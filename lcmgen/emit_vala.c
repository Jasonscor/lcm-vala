#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>
#ifdef WIN32
#define __STDC_FORMAT_MACROS			// Enable integer types
#endif
#include <inttypes.h>

#include <sys/stat.h>
#include <sys/types.h>

#include "lcmgen.h"


#define INDENT(n) (4*(n))

#define emit_start(n, ...) do { fprintf(f, "%*s", INDENT(n), ""); fprintf(f, __VA_ARGS__); } while (0)
#define emit_continue(...) do { fprintf(f, __VA_ARGS__); } while (0)
#define emit_end(...) do { fprintf(f, __VA_ARGS__); fprintf(f, "\n"); } while (0)
#define emit(n, ...) do { fprintf(f, "%*s", INDENT(n), ""); fprintf(f, __VA_ARGS__); fprintf(f, "\n"); } while (0)

#if 0
static char *dots_to_slashes(const char *s)
{
    char *p = strdup(s);

    for (char *t=p; *t!=0; t++)
        if (*t == '.')
            *t = G_DIR_SEPARATOR;

    return p;
}

static void make_dirs_for_file(const char *path)
{
#ifdef WIN32
    char *dirname = g_path_get_dirname(path);
    g_mkdir_with_parents(dirname, 0755);
    g_free(dirname);
#else
    int len = strlen(path);
    for (int i = 0; i < len; i++) {
        if (path[i]=='/') {
            char *dirpath = (char *) malloc(i+1);
            strncpy(dirpath, path, i);
            dirpath[i]=0;

            mkdir(dirpath, 0755);
            free(dirpath);

            i++; // skip the '/'
        }
    }
#endif
}
#endif

void setup_vala_options(getopt_t *gopt)
{
    getopt_add_string (gopt, 0, "vala-path",    ".",      "Location for .vala files");
}

static const char *map_type_name (const char* t)
{
    if      (!strcmp ("byte", t))    return "int8";
    else if (!strcmp ("boolean", t)) return "bool";
    else if (!strcmp ("int8_t", t))  return "int8";
    else if (!strcmp ("int16_t", t)) return "int16";
    else if (!strcmp ("int32_t", t)) return "int32";
    else if (!strcmp ("int64_t", t)) return "int64";
    //else if (!strcmp ("float", t))   return "float";
    //else if (!strcmp ("double", t))  return "double";
    //else if (!strcmp ("string", t))  return "string";

    return t;
}

static const char *make_dynarray_type(const char *buf, size_t maxlen, char *type, unsigned int ndim)
{
    FILE *f = fmemopen(buf, maxlen, "w");

    fprintf(f, "%s[", type);
    for (unsigned int d = 1; d < ndim; d++)
        fputc(',', f);
    fputc(']', f);

    fclose(f);
    return buf;
}

static void emit_auto_generated_warning(FILE *f)
{
    fprintf(f,
            "/* THIS IS AN AUTOMATICALLY GENERATED FILE.\n"
            " * DO NOT MODIFY BY HAND!!\n"
            " *\n"
            " * Generated by lcm-gen\n"
            " */\n\n");
}

static void emit_comment(FILE* f, int indent, const char* comment) {
    if (!comment)
        return;

    gchar** lines = g_strsplit(comment, "\n", 0);
    int num_lines = 0;
    for (num_lines = 0; lines[num_lines]; num_lines++) {}

    if (num_lines == 1) {
        emit(indent, "//! %s", lines[0]);
    } else {
        emit(indent, "/**");
        for (int line_ind = 0; lines[line_ind]; line_ind++) {
            if (strlen(lines[line_ind])) {
                emit(indent, " * %s", lines[line_ind]);
            } else {
                emit(indent, " *");
            }
        }
        emit(indent, " */");
    }
    g_strfreev(lines);
}

static void emit_class_start(lcmgen_t *lcm, FILE *f, lcm_struct_t *ls)
{
    const char *tn = ls->structname->lctypename;

    emit_comment(f, 0, ls->comment);
    emit(0, "public class %s : Lcm.IMessage {", tn);
}

static void emit_class_end(FILE *f)
{
    emit(0, "}");
}

static void emit_data_members(lcmgen_t *lcm, FILE *f, lcm_struct_t *ls)
{
    if (g_ptr_array_size(ls->members) == 0) {
        emit(1, "// no data members");
        emit(0, "");
        return;
    }

    emit(1, "// data members @{");
    for (unsigned int mind = 0; mind < g_ptr_array_size(ls->members); mind++) {
        lcm_member_t *lm = (lcm_member_t *) g_ptr_array_index(ls->members, mind);

        emit_comment(f, 1, lm->comment);
        char* mapped_typename = map_type_name(lm->type->lctypename);
        int ndim = g_ptr_array_size(lm->dimensions);
        if (ndim == 0) {
            emit(1, "public %-10s %s;", mapped_typename, lm->membername);
        } else {
            if (lcm_is_constant_size_array(lm)) {
                emit_start(1, "public %-10s %s[", mapped_typename, lm->membername);
                for (unsigned int d = 0; d < ndim; d++) {
                    lcm_dimension_t *ld = (lcm_dimension_t *) g_ptr_array_index(lm->dimensions, d);
                    emit_continue("%s%s", (d != 0)? ", " : "", ld->size);
                }
                emit_end("];");
            } else {
                char buf[256];
                emit(1, "public %-10s %s;",
                        make_dynarray_type(buf, sizeof(buf), mapped_typename, ndim),
                        lm->membername);
            }
        }
    }
    emit(1, "// @}");
    emit(0, "");
}

static void emit_const_members(lcmgen_t *lcm, FILE *f, lcm_struct_t *ls)
{
    if (g_ptr_array_size(ls->constants) == 0) {
        return;
    }

    emit(1, "// constants @{");
    for (unsigned int i = 0; i < g_ptr_array_size(ls->constants); i++) {
        lcm_constant_t *lc = (lcm_constant_t *) g_ptr_array_index(ls->constants, i);
        assert(lcm_is_legal_const_type(lc->lctypename));

        char* mapped_typename = map_type_name(lc->lctypename);

        emit_comment(f, 1, lc->comment);
        emit(1, "public const %-10s %s = (%s) %s;",
                mapped_typename, lc->membername,
                mapped_typename, lc->val_str);
    }
    emit(1, "// @}");
    emit(0, "");
}

static void emit_encode(FILE *f)
{
    emit(1, "public void[] encode() throws Lcm.MessageError {");
    emit(2,     "Posix.off_t pos = 0;");
    emit(2,     "int64 hash_ = this.hash;");
    emit(2,     "var buf = new void[this._encoded_size_no_hash + 8];");
    emit(0, "");
    emit(2,     "pos += Lcm.CoreTypes.int64_encode_array(buf, pos, &hash, 1);");
    emit(2,     "this._encode_no_hash(buf, pos);");
    emit(0, "");
    emit(2,     "return buf;");
    emit(1, "}");
    emit(0, "");
}

static void emit_hash_param(FILE *f)
{
    emit(1, "private static int64 _hash = _compute_hash(null);");
	emit(1,	"public int64 hash {");
	emit(2,     "get { return _hash; }");
	emit(1,	"}");
    emit(0, "");
}

static void emit_compute_hash(lcmgen_t *lcm, FILE *f, lcm_struct_t *ls)
{
    int last_complex_member = -1;
    for (unsigned int m = 0; m < g_ptr_array_size(ls->members); m++) {
        lcm_member_t *lm = (lcm_member_t *) g_ptr_array_index(ls->members, m);
        if (!lcm_is_primitive_type(lm->type->lctypename))
            last_complex_member = m;
    }

    emit(1, "public static int64 _compute_hash(Lcm.CoreTypes.intptr[]? parents) {");
    emit(2,     "if (((Lcm.CoreTypes.intptr) _compute_hash) in parents)");
    emit(3,         "return 0;");
    emit(0, "");
    if (last_complex_member >= 0) {
        emit(2,     "Lcm.CoreTypes.intptr[] cp = parents;");
        emit(2,     "cp += ((Lcm.CoreTypes.intptr) _compute_hash);");
        emit(0, "");
        emit(2,     "int64 hash_ = 0x%016"PRIx64" +", ls->hash);

        for (unsigned int m = 0; m <= last_complex_member; m++) {
            lcm_member_t *lm = (lcm_member_t *) g_ptr_array_index(ls->members, m);

            if (!lcm_is_primitive_type(lm->type->lctypename)) {
                emit(3, " %s._compute_hash(cp)%s",
                        lm->type->lctypename,
                        (m == last_complex_member) ? ";" : " +");
            }
        }
    } else {
        emit(2,     "int64 hash_ = 0x%016"PRIx64";", ls->hash);
    }
    emit(0, "");
    emit(2,     "return (hash_ << 1) + ((hash_ >> 63) & 1);");
    emit(1, "}");
}

int emit_vala(lcmgen_t *lcmgen)
{
    // iterate through all defined message types
    for (unsigned int i = 0; i < g_ptr_array_size(lcmgen->structs); i++) {
        lcm_struct_t *lr = (lcm_struct_t *) g_ptr_array_index(lcmgen->structs, i);

        const char *tn = lr->structname->lctypename;

        // compute the target filename
        char *file_name = g_strdup_printf("%s%s%s.vala",
                getopt_get_string(lcmgen->gopt, "vala-path"),
                strlen(getopt_get_string(lcmgen->gopt, "vala-path")) > 0 ? G_DIR_SEPARATOR_S : "",
                tn);

        // generate code if needed
        if (lcm_needs_generation(lcmgen, lr->lcmfile, file_name)) {
            //make_dirs_for_file(file_name);

            FILE *f = fopen(file_name, "w");
            if (f == NULL)
                return -1;

            emit_auto_generated_warning(f);
            emit_class_start(lcmgen, f, lr);

            emit_data_members(lcmgen, f, lr);
            emit_const_members(lcmgen, f, lr);

            emit_encode(f);
            //emit_decode(lcmgen, f, lr);

            //emit_encode_nohash(lcmgen, f, lr);
            //emit_decode_nohash(lcmgen, f, lr);
            //emit_encoded_size_nohash(lcmgen, f, lr);

            emit_hash_param(f);
            emit_compute_hash(lcmgen, f, lr);
            emit_class_end(f);

            fclose(f);
        }

        g_free(file_name);
    }

    return 0;

}
