/*
 * libieee1284 - IEEE 1284 library
 * Copyright (C) 2001  Tim Waugh <twaugh@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "config.h"
#include "ieee1284.h"
#include "detect.h"

#define MAX_PORTS 20

static int add_port (struct parport_list *list,
		     const char *name, const char *device, unsigned long base)
{
  struct parport *p;
  struct parport_internal *priv;

  if (list->portc == (MAX_PORTS - 1))
    // Ridiculous.
    return -1;

  p = malloc (sizeof *p);
  if (!p)
    return -1;

  p->name = strdup (name);
  if (!p->name)
    {
      free (p);
      return -1;
    }

  priv = malloc (sizeof *priv);
  if (!priv)
    {
      free ((char *) (p->name));
      free (p);
      return -1;
    }

  p->priv = priv;
  priv->device = strdup (device);
  if (!priv->device)
    {
      free (priv);
      free ((char *) (p->name));
      free (p);
      return -1;
    }

  priv->base = base;
  priv->base_hi = 0;
  priv->fd = -1;
  priv->type = 0;
  priv->claimed = -1;

  list->portv[list->portc++] = p;
  return 0;
}

static int populate_from_parport (struct parport_list *list)
{
  struct dirent *de;
  DIR *parport = opendir ("/proc/parport");
  if (!parport)
    return -1;

  de = readdir (parport);
  while (de)
    {
      if (strcmp (de->d_name, ".") && strcmp (de->d_name, ".."))
	{
	  char device[50];
	  char hardware[50];
	  unsigned long base = 0;
	  int fd;

	  // Device
	  if (capabilities & PPDEV_CAPABLE)
	    sprintf (device, "/dev/parport%s", de->d_name);
	  else
	    {
	      if (capabilities & IO_CAPABLE)
		device[0] = '\0';
	      else if (capabilities & DEV_PORT_CAPABLE)
		strcpy (device, "/dev/port");
	    }

	  // Base
	  sprintf (hardware, "/proc/parport/%s/hardware", de->d_name);
	  fd = open (hardware, O_RDONLY | O_NOCTTY);
	  if (fd >= 0)
	    {
	      char contents[500];
	      ssize_t got = read (fd, contents, sizeof contents - 1);
	      close (fd);
	      if (got > 0)
		{
		  char *p;
		  contents[got - 1] = '\0';
		  p = strstr (contents, "base:");
		  if (p)
		    {
		      p += strspn (p, " \t");
		      base = strtoul (p, NULL, 0);
		    }
		}
	    }

	  add_port (list, de->d_name, device, base);
	}

      de = readdir (parport);
    }

  closedir (parport);
  return 0;
}

static int populate_from_sys_dev_parport (struct parport_list *list)
{
  struct dirent *de;
  DIR *parport = opendir ("/proc/sys/dev/parport");
  if (!parport)
    return -1;

  de = readdir (parport);
  while (de)
    {
      if (strcmp (de->d_name, ".") &&
	  strcmp (de->d_name, "..") &&
	  strcmp (de->d_name, "default"))
	{
	  char device[50];
	  unsigned long base = 0;
	  size_t len = strlen (de->d_name) - 1;
	  char baseaddr[50];
	  int fd;
	  char *p;

	  while (len > 0 && isdigit (de->d_name[len]))
	    len--;

	  p = de->d_name + len + 1;

	  // Device
	  if (*p && capabilities & PPDEV_CAPABLE)
	    sprintf (device, "/dev/parport%s", p);
	  else
	    {
	      if (capabilities & IO_CAPABLE)
		device[0] = '\0';
	      else if (capabilities & DEV_PORT_CAPABLE)
		strcpy (device, "/dev/port");
	    }

	  // Base
	  sprintf (baseaddr, "/proc/sys/dev/parport/%s/base-addr", de->d_name);
	  fd = open (baseaddr, O_RDONLY | O_NOCTTY);
	  if (fd >= 0)
	    {
	      char contents[20];
	      ssize_t got = read (fd, contents, sizeof contents - 1);
	      close (fd);
	      if (got > 0)
		base = strtoul (contents, NULL, 0);
	    }
      
	  add_port (list, de->d_name, device, base);
	}

      de = readdir (parport);
    }

  closedir (parport);
  return 0;
}

static int populate_by_guessing (struct parport_list *list)
{
  add_port (list, "0x378", "/dev/port", 0x378);
  add_port (list, "0x278", "/dev/port", 0x278);
  add_port (list, "0x3bc", "/dev/port", 0x3bc);
  return 0;
}

// Find out what ports there are.
int ieee1284_find_ports (struct parport_list *list,
			 const char *config_file, int flags)
{
  detect_environment (0);
  list->portc = 0;
  list->portv = malloc (sizeof(char*) * MAX_PORTS);

  if (capabilities & PROC_SYS_DEV_PARPORT_CAPABLE)
    populate_from_sys_dev_parport (list);
  else if (capabilities & PROC_PARPORT_CAPABLE)
    populate_from_parport (list);
  else populate_by_guessing (list);

  if (list->portc == 0)
    {
      free (list->portv);
      list->portv = NULL;
    }

  return 0;
}

// Free up a parport_list structure.
void ieee1284_free_ports (struct parport_list *list)
{
  int i;

  for (i = 0; i < list->portc; i++)
    {
      struct parport *p = list->portv[i];
      struct parport_internal *priv = p->priv;
      if (p->name)
	free ((char *) (p->name));
      if (priv->device)
	free (priv->device);
      free (priv);
      free (p);
    }

  if (list->portv)
    free (list->portv);

  list->portv = NULL;
  list->portc = 0;
}
