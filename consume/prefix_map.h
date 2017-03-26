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

struct prefix_maps
{
  struct prefix_map *head;
  size_t max_replace;
};

/* Add a new mapping.
 *
 * The input strings are duplicated and a new prefix_map struct is allocated.
 * Ownership of the duplicates, as well as the new prefix_map, is the same as
 * the owner of the overall prefix_maps struct.
 *
 * Returns 0 on failure and 1 on success.
 */
int
add_prefix_map (const char *new_prefix, const char *old_prefix,
		struct prefix_maps *maps)
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

  map->next = maps->head;
  maps->head = map;

  if (map->new_len > maps->max_replace)
    maps->max_replace = map->new_len;

  return 1;

rewind_2:
  free ((void *) map->old_prefix);
rewind_1:
  free (map);
rewind_0:
  return 0;
}


/* Clear all mappings.
 *
 * All child structs of [maps] are freed, but it itself is not freed.
 */
void
clear_prefix_maps (struct prefix_maps *maps)
{
  struct prefix_map *map;
  struct prefix_map *next;

  for (map = maps->head; map; map = next)
    {
      free ((void *) map->old_prefix);
      free ((void *) map->new_prefix);
      next = map->next;
      free (map);
    }

  maps->max_replace = 0;
}


/* Private function, assumes new_name is wide enough to hold the remapped name. */
const char *
remap_prefix_to (const char *old_name, char *new_name,
		 struct prefix_map *map_head)
{
  struct prefix_map *map;
  const char *name;
  int len;

  for (map = map_head; map; map = map->next)
    {
      len = map->old_len;
      /* Ignore trailing path separators at the end of old_prefix */
      while (len > 0 && IS_DIR_SEPARATOR (map->old_prefix[len-1])) len--;
      /* Check if old_name matches old_prefix at a path component boundary */
      if (! strncmp (old_name, map->old_prefix, len)
	  && (IS_DIR_SEPARATOR (old_name[len])
	      || old_name[len] == '\0'))
	break;
    }
  if (!map)
    return old_name;

  name = old_name + len;
  memcpy (new_name, map->new_prefix, map->new_len);
  memcpy (new_name + map->new_len, name, strlen (name) + 1);
  return new_name;
}


#define remap_prefix_alloca(filename, maps) \
  remap_prefix_to ((filename), \
		   (char *) alloca (strlen ((filename)) + (maps)->max_replace + 1), \
		   (maps)->head)


/* Remap a filename.
 *
 * This function does not consume nor take ownership of filename; the caller is
 * responsible for freeing it, if and only if it was already responsible for
 * freeing it before the call.
 *
 * It allocates new memory only in the case that a mapping was made. That is,
 * if and only if filename != return-value, then the caller is responsible for
 * freeing return-value.
*/
const char *
remap_prefix_alloc (const char *filename, struct prefix_maps *maps, void *(*alloc)(size_t size))
{
  const char *name = remap_prefix_alloca (filename, maps);

  if (name == filename)
    return filename;

  size_t len = strlen (name) + 1;
  return (char *) memcpy (alloc (len), name, len);
}


/* Like remap_prefix_alloc but with the system allocator. */
const char *
remap_prefix (const char *filename, struct prefix_maps *maps)
{
  return remap_prefix_alloc (filename, maps, malloc);
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
generic_main (int (*parse_prefix_maps) (const char *, struct prefix_maps *), int argc, char *argv[])
{
  struct prefix_maps build_path_prefix_map = { NULL, 0 };

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
    if (!parse_prefix_maps (str, &build_path_prefix_map))
      {
	fprintf (stderr, "parse_prefix_maps failed\n");
	goto err_exit;
      }

  if (using_stdin)
    {
      free (str); // as per contract of getdelim()
      str = NULL;

      while (-1 != (read = getline (&str, &len_allocated, stdin)))
	{
	  *(str + read - 1) = 0;
	  printf ("%s\n", remap_prefix (str, &build_path_prefix_map));
	}

      if (ferror (stdin))
	goto err_stdin;
    }
  else
    {
      for (int i = using_stdin ? 2 : 1; i < argc; i++)
	{
	  const char *newarg = remap_prefix (argv[i], &build_path_prefix_map);
	  printf ("%s\n", newarg);
	  if (newarg != argv[i])
	    free ((void *) newarg); // as per contract of remap_prefix()
	}
    }

  clear_prefix_maps (&build_path_prefix_map);
  return 0;

err_stdin:
  perror ("failed to read from stdin");
err_exit:
  clear_prefix_maps (&build_path_prefix_map);
  return 1;
}
