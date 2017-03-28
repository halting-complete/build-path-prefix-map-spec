/* This file also contains implementation code, for our convenience. In a real
 * program it would be split into a separate .c file, possibly several. */

#define _POSIX_C_SOURCE 200809L

/* Some memory management primitives, basically copied from GCC. */

#include <malloc.h>
#include <alloca.h> // this is non-standard but is included in the GCC sources
#include <string.h>

#define XNEW(T)			((T *) malloc (sizeof (T)))
#define XNEWVEC(T, N)		((T *) malloc (sizeof (T) * (N)))

char *
xstrdup (const char *s)
{
  register size_t len = strlen (s) + 1;
  register char *ret = XNEWVEC (char, len);
  return (char *) memcpy (ret, s, len);
}

/* Some path parsing primitives, basically also copied from GCC. */

#if defined(__MSDOS__) || defined(_WIN32) || defined(__OS2__) || defined (__CYGWIN__)
#  define IS_DIR_SEPARATOR(c) IS_DOS_DIR_SEPARATOR (c)
#else /* not DOSish */
#  define IS_DIR_SEPARATOR(c) IS_UNIX_DIR_SEPARATOR (c)
#endif

#define IS_DIR_SEPARATOR_1(dos_based, c)				\
  (((c) == '/')								\
   || (((c) == '\\') && (dos_based)))

#define IS_DOS_DIR_SEPARATOR(c) IS_DIR_SEPARATOR_1 (1, c)
#define IS_UNIX_DIR_SEPARATOR(c) IS_DIR_SEPARATOR_1 (0, c)

/** Applying the variable */

struct prefix_map
{
  const char *old_prefix;
  const char *new_prefix;
  size_t old_len;
  size_t new_len;
  struct prefix_map *next;
};

/* Push a new mapping.

   The input strings are duplicated and a new prefix_map struct is allocated.
   Ownership of the duplicates, as well as the new prefix_map, is the same as
   the ownership of the old struct.

   Returns 0 on failure and 1 on success.  */
int
prefix_map_push (struct prefix_map **map_head,
		 const char *new_prefix, const char *old_prefix)
{
  struct prefix_map *map = XNEW (struct prefix_map);
  if (!map)
    goto rewind_0;

  map->old_prefix = xstrdup (old_prefix);
  if (!map->old_prefix)
    goto rewind_1;
  map->old_len = strlen (old_prefix);

  map->new_prefix = xstrdup (new_prefix);
  if (!map->new_prefix)
    goto rewind_2;
  map->new_len = strlen (new_prefix);

  map->next = *map_head;
  *map_head = map;
  return 1;

rewind_2:
  free ((void *) map->old_prefix);
rewind_1:
  free (map);
rewind_0:
  return 0;
}

/* Pop a prefix map.

   Everything up to but excluding the given OLD_HEAD is freed.  */
void
prefix_map_pop_until (struct prefix_map **map_head, struct prefix_map *old_head)
{
  struct prefix_map *map;
  struct prefix_map *next;

  for (map = *map_head; map != old_head; map = next)
    {
      free ((void *) map->old_prefix);
      free ((void *) map->new_prefix);
      next = map->next;
      free (map);
    }

  *map_head = map;
}

/* Clear all mappings.

   All child structs of MAP_HEAD are freed.  */
void
prefix_map_clear (struct prefix_map **map_head)
{
  prefix_map_pop_until (map_head, NULL);
}


/* Find a mapping suitable for the given OLD_NAME in the linked list MAP.\

   If a mapping is found, writes a pointer to the non-matching suffix part of
   OLD_NAME in SUFFIX, and its length in SUF_LEN.

   Returns NULL if there was no suitable mapping.  */
struct prefix_map *
prefix_map_find (struct prefix_map *map, const char *old_name,
		 const char **suffix, size_t *suf_len)
{
  size_t len;

  for (; map; map = map->next)
    {
      len = map->old_len;
      /* Ignore trailing path separators at the end of old_prefix */
      while (len > 0 && IS_DIR_SEPARATOR (map->old_prefix[len-1])) len--;
      /* Check if old_name matches old_prefix at a path component boundary */
      if (! strncmp (old_name, map->old_prefix, len)
	  && (IS_DIR_SEPARATOR (old_name[len])
	      || old_name[len] == '\0'))
	{
	  *suf_len = strlen (*suffix = old_name + len);
	  break;
	}
    }

  return map;
}

/* Prepend a prefix map before a given SUFFIX.

   The remapped name is written to NEW_NAME and returned as a const pointer. No
   allocations are performed; the caller must ensure it can hold at least
   MAP->NEW_LEN + SUF_LEN + 1 characters.  */
const char *
prefix_map_prepend (struct prefix_map *map, char *new_name,
		    const char *suffix, size_t suf_len)
{
  memcpy (new_name, map->new_prefix, map->new_len);
  memcpy (new_name + map->new_len, suffix, suf_len + 1);
  return new_name;
}

/* Remap a filename.

   Returns OLD_NAME unchanged if there was no remapping, otherwise returns a
   pointer to newly-allocated memory for the remapped filename.  The memory is
   allocated by the given ALLOC function, which also determines who is
   responsible for freeing it.  */
#define prefix_map_remap_alloc_(map_head, old_name, alloc)		       \
  __extension__								       \
  ({									       \
    const char *__suffix;						       \
    size_t __suf_len;							       \
    struct prefix_map *__map;						       \
    (__map = prefix_map_find ((map_head), (old_name), &__suffix, &__suf_len))  \
      ? prefix_map_prepend (__map,					       \
			    (char *) alloc (__map->new_len + __suf_len + 1),   \
			    __suffix, __suf_len)			       \
      : (old_name);							       \
  })

/* Remap a filename.

   Returns OLD_NAME unchanged if there was no remapping, otherwise returns a
   stack-allocated pointer to the newly-remapped filename.  */
#define prefix_map_remap_alloca(map_head, old_name) \
  prefix_map_remap_alloc_ (map_head, old_name, alloca)

/* Remap a filename.

   Returns OLD_NAME unchanged if there was no remapping, otherwise returns a
   pointer to newly-allocated memory for the remapped filename.  The caller is
   then responsible for freeing it.

   That is, if and only if OLD_NAME != return-value, the caller is responsible
   for freeing return-value.  The owner of filename remains unchanged.  */
const char *
prefix_map_remap_alloc (struct prefix_map *map_head,
			const char *old_name,
			void *(*alloc)(size_t size))
{
  return prefix_map_remap_alloc_ (map_head, old_name, alloc);
}

/** Main program */

#include <stdlib.h>
#include <stdio.h>

/*
 * Run as:
 *
 * $ BUILD_PATH_PREFIX_MAP=${map} ./main ${path0} ${path1} ${path2}
 *
 * Or a more clumsy interface, required by afl-fuzz:
 *
 * $ printf "${map}\n${path0}\n${path1}\n${path2}\n" | ./main -
 *
 * Returns 1 on failure and 0 on success.
 */
int
generic_main (int (*prefix_map_parse) (struct prefix_map **, const char *), int argc, char *argv[])
{
  struct prefix_map *build_path_prefix_map = NULL;

  int using_stdin = 0; // 0 = BUILD_PATH_PREFIX_MAP envvar, 1 = stdin (for afl)
  char *str = NULL;
  ssize_t read;
  size_t len_allocated = 0;

  if (argc > 1 && strncmp (argv[1], "-", 1) == 0)
    {
      read = getline (&str, &len_allocated, stdin);
      *(str + read - 1) = 0;
      if (ferror (stdin))
	goto err_stdin;
      using_stdin = 1;
    }
  else
    str = getenv ("BUILD_PATH_PREFIX_MAP");

  if (str)
    if (!prefix_map_parse (&build_path_prefix_map, str))
      {
	fprintf (stderr, "parse_prefix_map failed\n");
	goto err_exit;
      }

  if (using_stdin)
    {
      free (str); // as per contract of getdelim()
      str = NULL;

      while (-1 != (read = getline (&str, &len_allocated, stdin)))
	{
	  *(str + read - 1) = 0;
	  // use malloc here as an example
	  const char *newarg = prefix_map_remap_alloc (build_path_prefix_map, str, malloc);
	  printf ("%s\n", newarg);
	  if (newarg != str)
	    free ((void *) newarg); // as per contract of remap_prefix_alloc()
	}
      free (str);

      if (ferror (stdin))
	goto err_stdin;
    }
  else
    {
      // using alloca, no free needed
      for (int i = using_stdin ? 2 : 1; i < argc; i++)
	printf ("%s\n", prefix_map_remap_alloca (build_path_prefix_map, argv[i]));
    }

  prefix_map_clear (&build_path_prefix_map);
  return 0;

err_stdin:
  perror ("failed to read from stdin");
err_exit:
  prefix_map_clear (&build_path_prefix_map);
  return 1;
}
