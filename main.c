/*
 * main.c
 * main() routine, printer functions
 *
 * Copyright (c) 2011 pkgconf authors (see AUTHORS).
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * This software is provided 'as is' and without any warranty, express or
 * implied.  In no event shall the authors be liable for any damages arising
 * from the use of this software.
 */

#include "config.h"
#include "pkg.h"
#include "bsdstubs.h"

#define PKG_CFLAGS			(1<<1)
#define PKG_CFLAGS_ONLY_I		(1<<2)
#define PKG_CFLAGS_ONLY_OTHER		(1<<3)
#define PKG_LIBS			(1<<4)
#define PKG_LIBS_ONLY_LDPATH		(1<<5)
#define PKG_LIBS_ONLY_LIBNAME		(1<<6)
#define PKG_LIBS_ONLY_OTHER		(1<<7)
#define PKG_MODVERSION			(1<<8)
#define PKG_REQUIRES			(1<<9)
#define PKG_REQUIRES_PRIVATE		(1<<10)
#define PKG_VARIABLES			(1<<11)
#define PKG_DIGRAPH			(1<<12)
#define PKG_KEEP_SYSTEM_CFLAGS		(1<<13)
#define PKG_KEEP_SYSTEM_LIBS		(1<<14)
#define PKG_VERSION			(1<<15)
#define PKG_ABOUT			(1<<16)
#define PKG_ENV_ONLY			(1<<17)
#define PKG_ERRORS_ON_STDOUT		(1<<18)
#define PKG_SILENCE_ERRORS		(1<<19)
#define PKG_IGNORE_CONFLICTS		(1<<20)
#define PKG_STATIC			(1<<21)
#define PKG_NO_UNINSTALLED		(1<<22)
#define PKG_UNINSTALLED			(1<<23)
#define PKG_LIST			(1<<24)
#define PKG_HELP			(1<<25)
#define PKG_PRINT_ERRORS		(1<<26)
#define PKG_SIMULATE			(1<<27)

static unsigned int global_traverse_flags = PKGF_NONE;

static int want_flags;
static int maximum_traverse_depth = -1;

static char *want_variable = NULL;
static char *sysroot_dir = NULL;

FILE *error_msgout = NULL;

static bool
fragment_has_system_dir(pkg_fragment_t *frag)
{
	switch (frag->type)
	{
	case 'L':
		if ((want_flags & PKG_KEEP_SYSTEM_CFLAGS) == 0 && !strcasecmp(SYSTEM_LIBDIR, frag->data))
			return true;
	case 'I':
		if ((want_flags & PKG_KEEP_SYSTEM_LIBS) == 0 && !strcasecmp(SYSTEM_INCLUDEDIR, frag->data))
			return true;
	default:
		break;
	}

	return false;
}

static inline const char *
print_sysroot_dir(pkg_fragment_t *frag)
{
	if (sysroot_dir == NULL)
		return "";
	else switch (frag->type)
	{
	case 'L':
	case 'I':
		return sysroot_dir;
	default:
		break;
	}

	return "";
}

static void
print_fragment(pkg_fragment_t *frag)
{
	if (fragment_has_system_dir(frag))
		return;

	if (frag->type)
		printf("-%c%s%s ", frag->type, print_sysroot_dir(frag), frag->data);
	else
		printf("%s ", frag->data);
}

static void
print_list_entry(const pkg_t *entry)
{
	if (entry->uninstalled)
		return;

	printf("%-30s %s - %s\n", entry->id, entry->realname, entry->description);
}

static void
print_cflags(pkg_fragment_t *list)
{
	pkg_fragment_t *frag;

	PKG_FOREACH_LIST_ENTRY(list, frag)
	{
		if ((want_flags & PKG_CFLAGS_ONLY_I) == PKG_CFLAGS_ONLY_I && frag->type != 'I')
			continue;
		else if ((want_flags & PKG_CFLAGS_ONLY_OTHER) == PKG_CFLAGS_ONLY_OTHER && frag->type == 'I')
			continue;

		print_fragment(frag);
	}
}

static void
print_libs(pkg_fragment_t *list)
{
	pkg_fragment_t *frag;

	PKG_FOREACH_LIST_ENTRY(list, frag)
	{
		if ((want_flags & PKG_LIBS_ONLY_LDPATH) == PKG_LIBS_ONLY_LDPATH && frag->type != 'L')
			continue;
		else if ((want_flags & PKG_LIBS_ONLY_LIBNAME) == PKG_LIBS_ONLY_LIBNAME && frag->type != 'l')
			continue;
		else if ((want_flags & PKG_LIBS_ONLY_OTHER) == PKG_LIBS_ONLY_OTHER && (frag->type == 'l' || frag->type == 'L'))
			continue;

		print_fragment(frag);
	}
}

static void
print_modversion(pkg_t *pkg, void *unused, unsigned int flags)
{
	(void) unused;
	(void) flags;

	if (pkg->version != NULL)
		printf("%s\n", pkg->version);
}

static void
print_variables(pkg_t *pkg, void *unused, unsigned int flags)
{
	pkg_tuple_t *node;
	(void) unused;
	(void) flags;

	PKG_FOREACH_LIST_ENTRY(pkg->vars, node)
		printf("%s\n", node->key);
}

static void
print_requires(pkg_t *pkg)
{
	pkg_dependency_t *node;

	PKG_FOREACH_LIST_ENTRY(pkg->requires, node)
	{
		printf("%s", node->package);

		if (node->version != NULL)
			printf(" %s %s", pkg_get_comparator(node), node->version);

		printf("\n");
	}
}

static void
print_requires_private(pkg_t *pkg)
{
	pkg_dependency_t *node;

	PKG_FOREACH_LIST_ENTRY(pkg->requires_private, node)
	{
		printf("%s", node->package);

		if (node->version != NULL)
			printf(" %s %s", pkg_get_comparator(node), node->version);

		printf("\n");
	}
}

static void
print_digraph_node(pkg_t *pkg, void *unused, unsigned int flags)
{
	pkg_dependency_t *node;
	(void) unused;
	(void) flags;

	printf("\"%s\" [fontname=Sans fontsize=8]\n", pkg->id);

	PKG_FOREACH_LIST_ENTRY(pkg->requires, node)
	{
		printf("\"%s\" -- \"%s\" [fontname=Sans fontsize=8]\n", node->package, pkg->id);
	}
}

static void
apply_digraph(pkg_t *world, void *unused, int maxdepth, unsigned int flags)
{
	printf("graph deptree {\n");
	printf("edge [color=blue len=7.5 fontname=Sans fontsize=8]\n");
	printf("node [fontname=Sans fontsize=8]\n");

	pkg_traverse(world, print_digraph_node, unused, maxdepth, flags);

	printf("}\n");
}

static void
apply_modversion(pkg_t *world, void *unused, int maxdepth, unsigned int flags)
{
	pkg_traverse(world, print_modversion, unused, maxdepth, flags);
}

static void
apply_variables(pkg_t *world, void *unused, int maxdepth, unsigned int flags)
{
	pkg_traverse(world, print_variables, unused, maxdepth, flags);
}

typedef struct {
	const char *variable;
	char buf[PKG_BUFSIZE];
} var_request_t;

static void
print_variable(pkg_t *pkg, void *data, unsigned int flags)
{
	var_request_t *req = data;
	const char *var;
	(void) flags;

	var = pkg_tuple_find(pkg->vars, req->variable);
	if (var != NULL)
	{
		if (*(req->buf) == '\0')
		{
			strlcpy(req->buf, var, sizeof(req->buf));
			return;
		}

		strlcat(req->buf, " ", sizeof(req->buf));
		strlcat(req->buf, var, sizeof(req->buf));
	}
}

static void
apply_variable(pkg_t *world, void *variable, int maxdepth, unsigned int flags)
{
	var_request_t req = {
		.variable = variable,
	};

	*req.buf = '\0';

	pkg_traverse(world, print_variable, &req, maxdepth, flags);
	printf("%s\n", req.buf);
}

static void
apply_cflags(pkg_t *world, void *unused, int maxdepth, unsigned int flags)
{
	pkg_fragment_t *list;
	(void) unused;

	list = pkg_cflags(world, maxdepth, flags | PKGF_SEARCH_PRIVATE);
	print_cflags(list);

	pkg_fragment_free(list);
}

static void
apply_libs(pkg_t *world, void *unused, int maxdepth, unsigned int flags)
{
	pkg_fragment_t *list;
	(void) unused;

	list = pkg_libs(world, maxdepth, flags);
	print_libs(list);

	pkg_fragment_free(list);
}

static void
apply_requires(pkg_t *world, void *unused, int maxdepth, unsigned int flags)
{
	pkg_dependency_t *iter;
	(void) unused;
	(void) maxdepth;

	PKG_FOREACH_LIST_ENTRY(world->requires, iter)
	{
		pkg_t *pkg;

		pkg = pkg_verify_dependency(iter, flags, NULL);
		print_requires(pkg);

		pkg_free(pkg);
	}
}

static void
apply_requires_private(pkg_t *world, void *unused, int maxdepth, unsigned int flags)
{
	pkg_dependency_t *iter;
	(void) unused;
	(void) maxdepth;

	PKG_FOREACH_LIST_ENTRY(world->requires, iter)
	{
		pkg_t *pkg;

		pkg = pkg_verify_dependency(iter, flags | PKGF_SEARCH_PRIVATE, NULL);
		print_requires_private(pkg);

		pkg_free(pkg);
	}
}

static void
check_uninstalled(pkg_t *pkg, void *data, unsigned int flags)
{
	int *retval = data;
	(void) flags;

	if (pkg->uninstalled)
		*retval = EXIT_SUCCESS;
}

static void
apply_uninstalled(pkg_t *world, void *data, int maxdepth, unsigned int flags)
{
	pkg_traverse(world, check_uninstalled, data, maxdepth, flags);
}

static void
print_graph_node(pkg_t *pkg, void *data, unsigned int flags)
{
	(void) data;
	(void) flags;

	printf("Considering graph node '%s' (%p)\n", pkg->id, pkg);
}

static void
apply_simulate(pkg_t *world, void *data, int maxdepth, unsigned int flags)
{
	pkg_traverse(world, print_graph_node, data, maxdepth, flags);
}

static void
version(void)
{
	printf("%s\n", PKG_PKGCONFIG_VERSION_EQUIV);
}

static void
about(void)
{
	printf("%s %s%s\n", PACKAGE_NAME, PACKAGE_VERSION, HAVE_STRICT_MODE ? " [strict]" : " [pkg-config compatible]");
	printf("Copyright (c) 2011 - 2012 pkgconf authors (see AUTHORS in documentation directory).\n\n");
	printf("Permission to use, copy, modify, and/or distribute this software for any\n");
	printf("purpose with or without fee is hereby granted, provided that the above\n");
	printf("copyright notice and this permission notice appear in all copies.\n\n");
	printf("This software is provided 'as is' and without any warranty, express or\n");
	printf("implied.  In no event shall the authors be liable for any damages arising\n");
	printf("from the use of this software.\n\n");
	printf("Report bugs at <%s>.\n", PACKAGE_BUGREPORT);
}

static void
usage(void)
{
	printf("usage: %s [OPTIONS] [LIBRARIES]\n", PACKAGE_NAME);

	printf("\nbasic options:\n\n");

	printf("  --help                            this message\n");
	printf("  --about                           print pkgconf version and license to stdout\n");
	printf("  --version                         print supported pkg-config version to stdout\n");
	printf("  --atleast-pkgconfig-version       check whether or not pkgconf is compatible\n");
	printf("                                    with a specified pkg-config version\n");
	printf("  --errors-to-stdout                print all errors on stdout instead of stderr\n");
	printf("  --silence-errors                  explicitly be silent about errors\n");
	printf("  --list-all                        list all known packages\n");
	printf("  --simulate                        simulate walking the calculated dependency graph\n");

	printf("\nchecking specific pkg-config database entries:\n\n");

	printf("  --atleast-version                 require a specific version of a module\n");
	printf("  --exact-version                   require an exact version of a module\n");
	printf("  --max-version                     require a maximum version of a module\n");
	printf("  --exists                          check whether or not a module exists\n");
	printf("  --uninstalled                     check whether or not an uninstalled module will be used\n");
	printf("  --no-uninstalled                  never use uninstalled modules when satisfying dependencies\n");
	printf("  --maximum-traverse-depth          maximum allowed depth for dependency graph\n");
	printf("  --static                          be more aggressive when computing dependency graph\n");
	printf("                                    (for static linking)\n");
	printf("  --env-only                        look only for package entries in PKG_CONFIG_PATH\n");
	printf("  --ignore-conflicts                ignore 'conflicts' rules in modules\n");

	printf("\nquerying specific pkg-config database fields:\n\n");

	printf("  --define-variable=varname=value   define variable 'varname' as 'value'\n");
	printf("  --variable=varname                print specified variable entry to stdout\n");
	printf("  --cflags                          print required CFLAGS to stdout\n");
	printf("  --cflags-only-I                   print required include-dir CFLAGS to stdout\n");
	printf("  --cflags-only-other               print required non-include-dir CFLAGS to stdout\n");
	printf("  --libs                            print required linker flags to stdout\n");
	printf("  --libs-only-L                     print required LDPATH linker flags to stdout\n");
	printf("  --libs-only-l                     print required LIBNAME linker flags to stdout\n");
	printf("  --libs-only-other                 print required other linker flags to stdout\n");
	printf("  --print-requires                  print required dependency frameworks to stdout\n");
	printf("  --print-requires-private          print required dependency frameworks for static\n");
	printf("                                    linking to stdout\n");
	printf("  --print-variables                 print all known variables in module to stdout\n");
	printf("  --digraph                         print entire dependency graph in graphviz 'dot' format\n");
	printf("  --keep-system-cflags              keep -I%s entries in cflags output\n", SYSTEM_INCLUDEDIR);
	printf("  --keep-system-libs                keep -L%s entries in libs output\n", SYSTEM_LIBDIR);

	printf("\nreport bugs to <%s>.\n", PACKAGE_BUGREPORT);
}

int
main(int argc, char *argv[])
{
	int ret;
	pkg_queue_t *pkgq = NULL;
	pkg_queue_t *pkgq_head = NULL;
	char *builddir;
	char *required_pkgconfig_version = NULL;
	char *required_exact_module_version = NULL;
	char *required_max_module_version = NULL;
	char *required_module_version = NULL;

	want_flags = 0;

	struct pkg_option options[] = {
		{ "version", no_argument, &want_flags, PKG_VERSION|PKG_PRINT_ERRORS, },
		{ "about", no_argument, &want_flags, PKG_ABOUT|PKG_PRINT_ERRORS, },
		{ "atleast-version", required_argument, NULL, 2, },
		{ "atleast-pkgconfig-version", required_argument, NULL, 3, },
		{ "libs", no_argument, &want_flags, PKG_LIBS|PKG_PRINT_ERRORS, },
		{ "cflags", no_argument, &want_flags, PKG_CFLAGS|PKG_PRINT_ERRORS, },
		{ "modversion", no_argument, &want_flags, PKG_MODVERSION|PKG_PRINT_ERRORS, },
		{ "variable", required_argument, NULL, 7, },
		{ "exists", no_argument, NULL, 8, },
		{ "print-errors", no_argument, &want_flags, PKG_PRINT_ERRORS, },
		{ "short-errors", no_argument, NULL, 10, },
		{ "maximum-traverse-depth", required_argument, NULL, 11, },
		{ "static", no_argument, &want_flags, PKG_STATIC, },
		{ "print-requires", no_argument, &want_flags, PKG_REQUIRES, },
		{ "print-variables", no_argument, &want_flags, PKG_VARIABLES|PKG_PRINT_ERRORS, },
		{ "digraph", no_argument, &want_flags, PKG_DIGRAPH, },
		{ "help", no_argument, &want_flags, PKG_HELP, },
		{ "env-only", no_argument, &want_flags, PKG_ENV_ONLY, },
		{ "print-requires-private", no_argument, &want_flags, PKG_REQUIRES_PRIVATE, },
		{ "cflags-only-I", no_argument, &want_flags, PKG_CFLAGS|PKG_CFLAGS_ONLY_I|PKG_PRINT_ERRORS, },
		{ "cflags-only-other", no_argument, &want_flags, PKG_CFLAGS|PKG_CFLAGS_ONLY_OTHER|PKG_PRINT_ERRORS, },
		{ "libs-only-L", no_argument, &want_flags, PKG_LIBS|PKG_LIBS_ONLY_LDPATH|PKG_PRINT_ERRORS, },
		{ "libs-only-l", no_argument, &want_flags, PKG_LIBS|PKG_LIBS_ONLY_LIBNAME|PKG_PRINT_ERRORS, },
		{ "libs-only-other", no_argument, &want_flags, PKG_LIBS|PKG_LIBS_ONLY_OTHER|PKG_PRINT_ERRORS, },
		{ "uninstalled", no_argument, &want_flags, PKG_UNINSTALLED, },
		{ "no-uninstalled", no_argument, &want_flags, PKG_NO_UNINSTALLED, },
		{ "keep-system-cflags", no_argument, &want_flags, PKG_KEEP_SYSTEM_CFLAGS, },
		{ "keep-system-libs", no_argument, &want_flags, PKG_KEEP_SYSTEM_LIBS, },
		{ "define-variable", required_argument, NULL, 27, },
		{ "exact-version", required_argument, NULL, 28, },
		{ "max-version", required_argument, NULL, 29, },
		{ "ignore-conflicts", no_argument, &want_flags, PKG_IGNORE_CONFLICTS, },
		{ "errors-to-stdout", no_argument, &want_flags, PKG_ERRORS_ON_STDOUT, },
		{ "silence-errors", no_argument, &want_flags, PKG_SILENCE_ERRORS, },
		{ "list-all", no_argument, &want_flags, PKG_LIST|PKG_PRINT_ERRORS, },
		{ "simulate", no_argument, &want_flags, PKG_SIMULATE, },
		{ NULL, 0, NULL, 0 }
	};

	while ((ret = pkg_getopt_long_only(argc, argv, "", options, NULL)) != -1)
	{
		switch (ret)
		{
		case 2:
			required_module_version = pkg_optarg;
			break;
		case 3:
			required_pkgconfig_version = pkg_optarg;
			break;
		case 7:
			want_variable = pkg_optarg;
			break;
		case 11:
			maximum_traverse_depth = atoi(pkg_optarg);
			break;
		case 27:
			pkg_tuple_define_global(pkg_optarg);
			break;
		case 28:
			required_exact_module_version = pkg_optarg;
			break;
		case 29:
			required_max_module_version = pkg_optarg;
			break;
		case '?':
		case ':':
			return EXIT_FAILURE;
			break;
		default:
			break;
		}
	}

	if ((want_flags & PKG_PRINT_ERRORS) != PKG_PRINT_ERRORS)
		want_flags |= (PKG_SILENCE_ERRORS);

	if ((want_flags & PKG_SILENCE_ERRORS) == PKG_SILENCE_ERRORS && !getenv("PKG_CONFIG_DEBUG_SPEW"))
		want_flags |= (PKG_SILENCE_ERRORS);
	else
		want_flags &= ~(PKG_SILENCE_ERRORS);

	if ((want_flags & PKG_LIBS_ONLY_LIBNAME) == PKG_LIBS_ONLY_LIBNAME)
		want_flags &= ~(PKG_LIBS_ONLY_OTHER|PKG_LIBS_ONLY_LDPATH);
	else if ((want_flags & PKG_LIBS_ONLY_LDPATH) == PKG_LIBS_ONLY_LDPATH)
		want_flags &= ~(PKG_LIBS_ONLY_OTHER);

	if ((want_flags & PKG_CFLAGS_ONLY_I) == PKG_CFLAGS_ONLY_I)
		want_flags &= ~(PKG_CFLAGS_ONLY_OTHER);

	if ((want_flags & PKG_ABOUT) == PKG_ABOUT)
	{
		about();
		return EXIT_SUCCESS;
	}

	if ((want_flags & PKG_VERSION) == PKG_VERSION)
	{
		version();
		return EXIT_SUCCESS;
	}

	if ((want_flags & PKG_HELP) == PKG_HELP)
	{
		usage();
		return EXIT_SUCCESS;
	}

	error_msgout = stderr;
	if ((want_flags & PKG_ERRORS_ON_STDOUT) == PKG_ERRORS_ON_STDOUT)
		error_msgout = stdout;
	if ((want_flags & PKG_SILENCE_ERRORS) == PKG_SILENCE_ERRORS)
		error_msgout = fopen(PATH_DEV_NULL, "w");

	if ((want_flags & PKG_IGNORE_CONFLICTS) == PKG_IGNORE_CONFLICTS || getenv("PKG_CONFIG_IGNORE_CONFLICTS") != NULL)
		global_traverse_flags |= PKGF_SKIP_CONFLICTS;

	if ((want_flags & PKG_STATIC) == PKG_STATIC)
		global_traverse_flags |= (PKGF_SEARCH_PRIVATE | PKGF_MERGE_PRIVATE_FRAGMENTS);

	if ((want_flags & PKG_ENV_ONLY) == PKG_ENV_ONLY)
		global_traverse_flags |= PKGF_ENV_ONLY;

	if ((want_flags & PKG_NO_UNINSTALLED) == PKG_NO_UNINSTALLED || getenv("PKG_CONFIG_DISABLE_UNINSTALLED") != NULL)
		global_traverse_flags |= PKGF_NO_UNINSTALLED;

	if (getenv("PKG_CONFIG_ALLOW_SYSTEM_CFLAGS") != NULL)
		want_flags |= PKG_KEEP_SYSTEM_CFLAGS;

	if (getenv("PKG_CONFIG_ALLOW_SYSTEM_LIBS") != NULL)
		want_flags |= PKG_KEEP_SYSTEM_LIBS;

	if ((builddir = getenv("PKG_CONFIG_TOP_BUILD_DIR")) != NULL)
		pkg_tuple_add_global("pc_top_builddir", builddir);
	else
		pkg_tuple_add_global("pc_top_builddir", "$(top_builddir)");

	if ((sysroot_dir = getenv("PKG_CONFIG_SYSROOT_DIR")) != NULL)
		pkg_tuple_add_global("pc_sysrootdir", sysroot_dir);
	else
		pkg_tuple_add_global("pc_sysrootdir", "/");

	if (required_pkgconfig_version != NULL)
	{
		if (pkg_compare_version(PKG_PKGCONFIG_VERSION_EQUIV, required_pkgconfig_version) >= 0)
			return EXIT_SUCCESS;

		return EXIT_FAILURE;
	}

	if ((want_flags & PKG_LIST) == PKG_LIST)
	{
		pkg_scan_all(print_list_entry);
		return EXIT_SUCCESS;
	}

	if (required_module_version != NULL)
	{
		pkg_t *pkg;
		pkg_dependency_t *pkghead = NULL, *pkgiter = NULL;

		while (argv[pkg_optind])
		{
			pkghead = pkg_dependency_parse_str(pkghead, argv[pkg_optind]);
			pkg_optind++;
		}

		PKG_FOREACH_LIST_ENTRY(pkghead, pkgiter)
		{
			pkg = pkg_find(pkgiter->package, global_traverse_flags);
			if (pkg == NULL)
				return EXIT_FAILURE;

			if (pkg_compare_version(pkg->version, required_module_version) >= 0)
				return EXIT_SUCCESS;
		}

		return EXIT_FAILURE;
	}

	if (required_exact_module_version != NULL)
	{
		pkg_t *pkg;
		pkg_dependency_t *pkghead = NULL, *pkgiter = NULL;

		while (argv[pkg_optind])
		{
			pkghead = pkg_dependency_parse_str(pkghead, argv[pkg_optind]);
			pkg_optind++;
		}

		PKG_FOREACH_LIST_ENTRY(pkghead, pkgiter)
		{
			pkg = pkg_find(pkgiter->package, global_traverse_flags);
			if (pkg == NULL)
				return EXIT_FAILURE;

			if (pkg_compare_version(pkg->version, required_exact_module_version) == 0)
				return EXIT_SUCCESS;
		}

		return EXIT_FAILURE;
	}

	if (required_max_module_version != NULL)
	{
		pkg_t *pkg;
		pkg_dependency_t *pkghead = NULL, *pkgiter = NULL;

		while (argv[pkg_optind])
		{
			pkghead = pkg_dependency_parse_str(pkghead, argv[pkg_optind]);
			pkg_optind++;
		}

		PKG_FOREACH_LIST_ENTRY(pkghead, pkgiter)
		{
			pkg = pkg_find(pkgiter->package, global_traverse_flags);
			if (pkg == NULL)
				return EXIT_FAILURE;

			if (pkg_compare_version(pkg->version, required_max_module_version) <= 0)
				return EXIT_SUCCESS;
		}

		return EXIT_FAILURE;
	}

	while (1)
	{
		const char *package = argv[pkg_optind];

		if (package == NULL)
			break;

		while (isspace(package[0]))
			package++;

		/* skip empty packages */
		if (package[0] == '\0') {
			pkg_optind++;
			continue;
		}

		if (argv[pkg_optind + 1] == NULL || !PKG_OPERATOR_CHAR(*(argv[pkg_optind + 1])))
		{
			pkgq = pkg_queue_push(pkgq, package);

			if (pkgq_head == NULL)
				pkgq_head = pkgq;

			pkg_optind++;
		}
		else
		{
			char packagebuf[PKG_BUFSIZE];

			snprintf(packagebuf, sizeof packagebuf, "%s %s %s", package, argv[pkg_optind + 1], argv[pkg_optind + 2]);
			pkg_optind += 3;

			pkgq = pkg_queue_push(pkgq, packagebuf);

			if (pkgq_head == NULL)
				pkgq_head = pkgq;
		}
	}

	if (pkgq_head == NULL)
	{
		fprintf(error_msgout, "Please specify at least one package name on the command line.\n");
		return EXIT_FAILURE;
	}

	ret = EXIT_SUCCESS;

	if (!pkg_queue_validate(pkgq_head, maximum_traverse_depth, global_traverse_flags))
	{
		ret = EXIT_FAILURE;
		goto out;
	}

	if ((want_flags & PKG_UNINSTALLED) == PKG_UNINSTALLED)
	{
		ret = EXIT_FAILURE;
		pkg_queue_apply(pkgq_head, apply_uninstalled, maximum_traverse_depth, global_traverse_flags, &ret);
		goto out;
	}

	if ((want_flags & PKG_DIGRAPH) == PKG_DIGRAPH)
	{
		want_flags &= ~(PKG_CFLAGS|PKG_LIBS);

		if (!pkg_queue_apply(pkgq_head, apply_digraph, maximum_traverse_depth, global_traverse_flags, NULL))
		{
			ret = EXIT_FAILURE;
			goto out;
		}
	}

	if ((want_flags & PKG_SIMULATE) == PKG_SIMULATE)
	{
		want_flags &= ~(PKG_CFLAGS|PKG_LIBS);

		if (!pkg_queue_apply(pkgq_head, apply_simulate, maximum_traverse_depth, global_traverse_flags, NULL))
		{
			ret = EXIT_FAILURE;
			goto out;
		}
	}

	if ((want_flags & PKG_MODVERSION) == PKG_MODVERSION)
	{
		want_flags &= ~(PKG_CFLAGS|PKG_LIBS);

		if (!pkg_queue_apply(pkgq_head, apply_modversion, 2, global_traverse_flags, NULL))
		{
			ret = EXIT_FAILURE;
			goto out;
		}
	}

	if ((want_flags & PKG_VARIABLES) == PKG_VARIABLES)
	{
		want_flags &= ~(PKG_CFLAGS|PKG_LIBS);

		if (!pkg_queue_apply(pkgq_head, apply_variables, 2, global_traverse_flags, NULL))
		{
			ret = EXIT_FAILURE;
			goto out;
		}
	}

	if (want_variable)
	{
		want_flags &= ~(PKG_CFLAGS|PKG_LIBS);

		if (!pkg_queue_apply(pkgq_head, apply_variable, 2, global_traverse_flags | PKGF_SKIP_ROOT_VIRTUAL, want_variable))
		{
			ret = EXIT_FAILURE;
			goto out;
		}
	}

	if ((want_flags & PKG_REQUIRES) == PKG_REQUIRES)
	{
		want_flags &= ~(PKG_CFLAGS|PKG_LIBS);

		if (!pkg_queue_apply(pkgq_head, apply_requires, maximum_traverse_depth, global_traverse_flags, NULL))
		{
			ret = EXIT_FAILURE;
			goto out;
		}
	}

	if ((want_flags & PKG_REQUIRES_PRIVATE) == PKG_REQUIRES_PRIVATE)
	{
		want_flags &= ~(PKG_CFLAGS|PKG_LIBS);

		if (!pkg_queue_apply(pkgq_head, apply_requires_private, maximum_traverse_depth, global_traverse_flags, NULL))
		{
			ret = EXIT_FAILURE;
			goto out;
		}
	}

	if ((want_flags & PKG_CFLAGS) == PKG_CFLAGS)
	{
		if (!pkg_queue_apply(pkgq_head, apply_cflags, maximum_traverse_depth, global_traverse_flags, NULL))
		{
			ret = EXIT_FAILURE;
			goto out;
		}
	}

	if ((want_flags & PKG_LIBS) == PKG_LIBS)
	{
		if (!pkg_queue_apply(pkgq_head, apply_libs, maximum_traverse_depth, global_traverse_flags, NULL))
		{
			ret = EXIT_FAILURE;
			goto out;
		}
	}

	if (want_flags & (PKG_CFLAGS|PKG_LIBS))
		printf("\n");

	pkg_queue_free(pkgq_head);

out:
	pkg_tuple_free_global();
	return ret;
}
