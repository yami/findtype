#include "defs.h"

#include <ctype.h>
#include <string.h>
#include <assert.h>

#include "command.h"
#include "gdbtypes.h"
#include "gdbcmd.h"
#include "symtab.h"
#include "typeprint.h"
#include "expression.h"
#include "value.h"
#include "exceptions.h"

/* findtype is a home-brew gdb command for finding types according to
 * size, name and/or types of data members.
 *
 * GDB_OLD should be defined when findtype.c is compiled in gdb-6.4.
 * Do not define it when it is compiled in gdb-7.2.
 */

void
type_print (struct type *type, char *varstring, struct ui_file *stream,
	    int show);


struct ft_findtype_spec {
    int recursive;                /* 1 if search is recursive (deafult is 1). */
    int size;                     /* 0 if size is not specified. */
    char *name;                   /* NULL if name is not specified. */
    struct ft_member_list *mlist; /* NULL if member is not specified. */
};

struct ft_member_list {
    char                   *name; /* it is the return value of type_to_string(type) */
    struct type            *type; /* type get from ft_string_to_type(member string from cmdline). */
    struct ft_member_list*  next; /* point to next ft_member_list in the list. NULL if this is the last. */
    int                     mark; /* used for ft_type_contains_members()  */
};

#ifdef GDB_OLD
char *
type_to_string (struct type *type)
{
  long length;
  char *s = NULL;
  struct ui_file *stb;
  struct cleanup *old_chain;
  volatile struct gdb_exception except;

  stb = mem_fileopen ();
  old_chain = make_cleanup_ui_file_delete (stb);

  TRY_CATCH (except, RETURN_MASK_ALL)
    {
      type_print (type, "", stb, -1);
      s = ui_file_xstrdup (stb, &length);
    }
  if (except.reason < 0)
    s = NULL;

  do_cleanups (old_chain);

  return s;
}

/* copied from typeprint.c */
static struct type *
ft_ptype_eval(struct expression *exp)
{
  if (exp->elts[0].opcode == OP_TYPE) {
      return (exp->elts[1].type);
  } else {
      return (NULL);
  }
}

static struct type *
ft_string_to_type(char *exp)
{
    struct gdb_exception except;
    struct expression *expression;
    struct cleanup *old_chain = NULL;
    struct type *type = NULL;

    TRY_CATCH(except, RETURN_MASK_ERROR) {
        expression = parse_expression(exp);
        old_chain = make_cleanup(free_current_contents, &expression);
        type = ft_ptype_eval(expression);
    }

    if (old_chain) {
        do_cleanups(old_chain);
    }

    return type;
}


#else

/* Get the pointer to 'struct type' from a string. This function
 * reassembles whatis_exp().
 */
static struct type *
ft_string_to_type(char *exp)
{
    volatile struct gdb_exception except;

    struct expression *expression;
    struct cleanup *old_chain = NULL;
    struct value *value;
    struct type *type = NULL;

    TRY_CATCH(except, RETURN_MASK_ERROR) {
        expression = parse_expression(exp);
        old_chain = make_cleanup(free_current_contents, &expression);
        value = evaluate_type(expression);
        type = value_type(value);
    }

    if (old_chain) {
        do_cleanups(old_chain);
    }

    return type;
}

#endif

static void
ft_member_list_free(struct ft_member_list *mlist)
{
    while (mlist) {
        struct ft_member_list *current = mlist;
        mlist = mlist->next;

        free(current->name);
        free(current);
    }
}

static struct ft_findtype_spec *
ft_findtype_spec_new()
{
    struct ft_findtype_spec* spec = (struct ft_findtype_spec*) malloc(sizeof(*spec));

    spec->recursive = 1;
    spec->size = 0;
    spec->name = NULL;
    spec->mlist = NULL;

    return spec;
}

static void
ft_findtype_spec_free(struct ft_findtype_spec *spec)
{
    if (spec) {
        free(spec->name);
        ft_member_list_free(spec->mlist);
        free(spec);
    }
}

static void __attribute__((used))
ft_findtype_spec_print(struct ft_findtype_spec *spec)
{
    struct ft_member_list *mlist;
    
    if (spec) {
        printf_filtered("spec = {\n");
        printf_filtered("  name=%s\n", spec->name ? spec->name : "<null>");
        printf_filtered("  size=%d\n", spec->size);
        printf_filtered("  member=");
        
        if (spec->mlist) {
            for (mlist = spec->mlist; mlist; mlist = mlist->next) {
                printf_filtered("%s;", mlist->name);
            }
        } else {
            printf_filtered("<null>");
        }

        printf_filtered("\n");
    } else {
        printf_filtered("null\n");
    }

    printf_filtered("}\n");
}

static void
ft_member_list_unmark_all(struct ft_member_list *mlist)
{
    for (; mlist; mlist = mlist->next) {
        mlist->mark = 0;
    }
}

static int
ft_member_list_is_all_marked(struct ft_member_list *mlist)
{
    for (; mlist; mlist = mlist->next) {
        if (!mlist->mark) {
            return 0;
        }
    }

    return 1;
}

static char *
ft_skip_spaces(char *beg)
{
    while (*beg && isspace(*beg)) {
        beg++;
    }

    return beg;
}


struct ft_member_list *
ft_member_list_new(char* name)
{
    struct ft_member_list *list = NULL; 
    struct type *type = ft_string_to_type(name);

    if (type == NULL) {
        printf_filtered("findtype: failed to lookup type: %s\n", name);
        return NULL;
    }

    list = (struct ft_member_list *) malloc(sizeof(*list));    
    list->type = type;

    if (list->type) {
        list->name = type_to_string(list->type);
    } else {
        list->name = strdup(name);
    }
    list->mark = 0;
    list->next = NULL;

    return list;
}

struct ft_member_list *
ft_make_member_list(const char* member_spec)
{
    struct ft_member_list *head = NULL;
    struct ft_member_list *entry = NULL;
    
    char *mspec = strdup(member_spec);
    char *comma;
    char *start = mspec;
    
    while (1) {
        if (*start == 0) {
            free(mspec);
            return head;
        }
        
        comma  = strchr(start, ';');

        if (comma) {
            *comma = 0;
        }

        entry = ft_member_list_new(start);
        if (entry == NULL) {
            ft_member_list_free(head);
            return NULL;
        }


        entry->next = head;
        head = entry;

        if (comma) {
            start = comma+1;
        } else {
            return head;
        }
    }

    return head;
}

/* Copy the string started at BEG and right before the position of
 * next DELIM or '\0' to OUT.
 *
 * Return NULL if DELIM is not found. Otherwise return the next
 * position of DELIM.
 */
char *
ft_get_string(const char *beg, char delim, char *out)
{
    char *end = strchr(beg, delim);

    if (end == NULL) {
        strcpy(out, beg);
        return NULL;
    }

    memcpy(out, beg, end - beg);
    out[end - beg] = 0;

    return end+1;
}


/* Parse the name-value pair like "name=value" or "name='value'" for
 * string BEG.  The name is copied to NAME, and value without quotes
 * to VALUE.
 *
 * Return NULL if *BEG is 0, or no '=' is in BEG. Otherwise return
 * next position to the ending space or quote.
 */
char *
ft_get_name_value(const char *beg, char *name, char *value)
{    
    if (*beg == 0) {
        return NULL;
    }

    beg = ft_get_string(beg, '=', name);
    if (beg == NULL) {
        return NULL;
    }

    if (*beg == '\'') {
        beg = ft_get_string(beg+1, '\'', value);
    } else {
        const char *tmp = beg;
        beg = ft_get_string(tmp, ' ', value);
        if (beg == NULL) {
            for (beg = tmp; *beg; beg++)
                ;
        }
    }

    return (char *)beg;
}

/* Parse the command line options and populate ft_findtype_spec.
 *
 * Return NULL if anything is wrong.
 */
static struct ft_findtype_spec *
ft_make_findtype_spec(char *strspec)
{
    char name[1024];
    char value[2048];
    
    char *str = strdup(strspec);
    char *beg = str;
    char *end;
    
    struct ft_findtype_spec *spec = ft_findtype_spec_new();

    if (*beg == '/') {
        if (*(strspec + 1) == 'n') {
            spec->recursive = 0;
            beg += 2;
        } else {
            printf_filtered("findtype: bad slash format: %s\n", strspec);
            goto error;
        }
    }
    
    for (beg = ft_skip_spaces(beg); *beg; beg = ft_skip_spaces(beg)) {
        beg = ft_get_name_value(beg, name, value);

        if (beg == NULL) {
            printf_filtered("findtype: bad option format!\n");
            goto error;
        } else if (strcmp(name, "size") == 0) {
            char *ep;
            spec->size = strtod(value, &ep);
            if (*ep) {
                printf_filtered("findtype: size value is not an integer: %s!\n", value);
                goto error;
            }
        } else if (strcmp(name, "name") == 0) {
            spec->name = strdup(value);
        } else if (strcmp(name, "member") == 0) {
            spec->mlist = ft_make_member_list(value);
            if (spec->mlist == NULL) {
                goto error;
            }
        } else {
            printf_filtered("findtype: bad option name!\n");
        }
    }

    free(str);
    return spec;
    
error:
    ft_findtype_spec_free(spec);
    free(str);
    return NULL;
}

/* Mark the corresponding member in the MLIST if it is same as TYPE.
 * 
 * Return 1 if all members are marked (TYPE is found in MLIST). 0 if
 * there are unmarked members.
 */
static int
ft_member_list_mark(struct ft_member_list *mlist, struct type *type)
{
    int all_marked = 1;
    for (; mlist; mlist = mlist->next) {
        if (mlist->mark) {
            continue;
        }
#if FT_DEBUG       
        printf_filtered("===========================\n");
        printf_filtered("    MLIST  vs. type        \n");
        printf_filtered("===========================\n");
        type_print(mlist->type, "", gdb_stdout, -1);
        printf_filtered("   vs.  ");
        type_print(type, "", gdb_stdout, -1);
        printf_filtered("\nmlist=%p type=%p; MAIN(mlist)=%p MAIN(type)=%p\n",
                        mlist->type, type, TYPE_MAIN_TYPE(mlist->type), TYPE_MAIN_TYPE(type));
#endif
        /* XXX: It seems that 
         *   1. main_type is different, but the type name might be same
         *      So we need an 'else' condition for main_type.
         *   2. the type name is different, but the type might be same.
         *      For example 'class Foo *' and 'Foo *'. To handle this case,
         *      I convert TYPE to STR, then convert STR back to TEMP type.
         *      The main type of MLIST->TYPE and TEMP should be same, because 
         *      we evaluate type names in the same context of MLIST->TYPE.
         */
        if (TYPE_MAIN_TYPE(mlist->type) ==  TYPE_MAIN_TYPE(type)) {
            mlist->mark = 1;
        } else {
            char *str  = type_to_string(type);
            if (strcmp(mlist->name, str) == 0) {
                mlist->mark = 1;
            } else {
                struct type *temp = ft_string_to_type(str);
                if (temp && (TYPE_MAIN_TYPE(mlist->type) == TYPE_MAIN_TYPE(temp))) {
                    mlist->mark = 1;
                }
            }

            free(str);
        }

        if (!mlist->mark) {
            all_marked = 0;
        }
    }

    return all_marked;
}

/* Return 1 if TYPE is not interested in the search. */
static int
ft_is_skipped_type(struct type *type)
{
    switch (TYPE_CODE(type)) {
#ifdef GDB_OLD
        case TYPE_CODE_MEMBER: 
#else
        case TYPE_CODE_FLAGS:
        case TYPE_CODE_METHODPTR:
        case TYPE_CODE_MEMBERPTR:
        case TYPE_CODE_INTERNAL_FUNCTION:
#endif
        case TYPE_CODE_FUNC:
        case TYPE_CODE_METHOD:
        case TYPE_CODE_NAMESPACE:
            return 1;
        default:
            return 0;
    }
}

static int
ft_is_compound_type(struct type * type)
{
    return TYPE_CODE(type) == TYPE_CODE_STRUCT ||
        TYPE_CODE(type) == TYPE_CODE_UNION;
}

/* Search fields of TYPE for members in MLIST. If recursive is true,
 * then also search fields of the fields.
 * 
 * Return 1 if all members in MLIST are marked (found). */
static int
ft_type_mark_members(struct type *type, struct ft_member_list *mlist, int recursive)
{
    int i;

    for (i = 0; i < TYPE_NFIELDS(type); i++) {
        struct type *field_type = TYPE_FIELD_TYPE(type, i);

        if (field_type == NULL) {
            continue;
        }

        /* gdb 6.4 defined TYPE_FIELD_STATIC, but in 7.2 we should use field_is_static. */
#ifdef GDB_OLD
        if (TYPE_FIELD_STATIC(type, i)) {
#else
        if (field_is_static(&TYPE_FIELD(type, i))) {
#endif
            continue;
        }

        if (ft_is_skipped_type(field_type)) {
            continue;
        }

        if (ft_member_list_mark(mlist, field_type)) {
            return 1;
        }

        if (!recursive) {
            continue;
        }
        
        if (ft_type_mark_members(field_type, mlist, 1)) {
            return 1;
        }
    }

    return ft_member_list_is_all_marked(mlist);
}

static int
ft_type_contains_members(struct type *type, struct ft_member_list *mlist, int recursive)
{
    ft_member_list_unmark_all(mlist);

    return ft_type_mark_members(type, mlist, recursive);
}


static void
ft_findtype(struct ft_findtype_spec *spec, int from_tty)
{
    struct symbol_search *symbols;
    struct symbol_search *p;
    struct cleanup *old_chain;
    struct type* type;
    
    search_symbols(spec->name, TYPES_DOMAIN, 0, (char **)NULL, &symbols);
    old_chain = make_cleanup_free_search_symbols(symbols);

    for (p = symbols; p != NULL; p = p->next) {
        QUIT;

        if (!p->symbol) {
            continue;
        }

        type = SYMBOL_TYPE(p->symbol);

        if (!ft_is_compound_type(type)) {
            continue;
        }
        
        if (spec->size && spec->size != TYPE_LENGTH(type)) {
            continue;
        }

        if (spec->mlist && !ft_type_contains_members(type, spec->mlist, spec->recursive)) {
            continue;
        }
        
        type_print(type, "", gdb_stdout, -1);
        printf_filtered("\n");
    }

    do_cleanups(old_chain);
}

static void
ft_findtype_command(char *strspec, int from_tty)
{
    struct ft_findtype_spec *spec = ft_make_findtype_spec(strspec);

#if FT_DEBUG
    ft_findtype_spec_print(spec);
#endif

    if (spec) {
        ft_findtype(spec, from_tty);
    }

    ft_findtype_spec_free(spec);
}


void
_initialize_findtype(void)
{
    add_com ("findtype", class_vars, ft_findtype_command, _("\
Find type according to simple rules. Usage\n\
  findtype [/n] [size=<size>] [name=<name>] [member=<MemberType1[;MemberType2;...]]\n\
When '/n' is specified, search members non-recurisvely. Searching is recursive by default.\n\
Use 'MemberType' if member type contains spaces. Example:\n\
  findtype size=12 member='struct Foo'\n\
Use regular expression for names (same as 'info types'). Example:\n\
  findtype name='Foo.*'\n\
"));
}
