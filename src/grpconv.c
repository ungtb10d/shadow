/*
 * Copyright (c) 1996 - 2000, Marek Michałkiewicz
 * Copyright (c) 2002 - 2006, Tomasz Kłoczko
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the copyright holders or contributors may not be used to
 *    endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * grpconv - create or update /etc/gshadow with information from
 * /etc/group.
 *
 */

#include <config.h>
#ident "$Id$"

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "nscd.h"
#include "prototypes.h"
#ifdef SHADOWGRP
#include "groupio.h"
#include "sgroupio.h"
/*
 * Global variables
 */
char *Prog;

static bool gr_locked  = false;
static bool sgr_locked = false;

/* local function prototypes */
static void fail_exit (int status);

static void fail_exit (int status)
{
	if (gr_locked) {
		if (gr_unlock () == 0) {
			fprintf (stderr, _("%s: failed to unlock %s\n"), Prog, gr_dbname ());
			SYSLOG ((LOG_ERR, "failed to unlock %s", gr_dbname ()));
			/* continue */
		}
	}

	if (sgr_locked) {
		if (sgr_unlock () == 0) {
			fprintf (stderr, _("%s: failed to unlock %s\n"), Prog, sgr_dbname ());
			SYSLOG ((LOG_ERR, "failed to unlock %s", sgr_dbname ()));
			/* continue */
		}
	}

	exit (status);
}

int main (int argc, char **argv)
{
	const struct group *gr;
	struct group grent;
	const struct sgrp *sg;
	struct sgrp sgent;

	if (1 != argc) {
		(void) fputs (_("Usage: grpconv\n"), stderr);
	}
	Prog = Basename (argv[0]);

	(void) setlocale (LC_ALL, "");
	(void) bindtextdomain (PACKAGE, LOCALEDIR);
	(void) textdomain (PACKAGE);

	OPENLOG ("grpconv");

	if (gr_lock () == 0) {
		fprintf (stderr,
		         _("%s: cannot lock %s; try again later.\n"),
		         Prog, gr_dbname ());
		fail_exit (5);
	}
	gr_locked = true;
	if (gr_open (O_RDWR) == 0) {
		fprintf (stderr, _("%s: cannot open %s\n"), Prog, gr_dbname ());
		fail_exit (1);
	}

	if (sgr_lock () == 0) {
		fprintf (stderr,
		         _("%s: cannot lock %s; try again later.\n"),
		         Prog, sgr_dbname ());
		fail_exit (5);
	}
	sgr_locked = true;
	if (sgr_open (O_CREAT | O_RDWR) == 0) {
		fprintf (stderr, _("%s: cannot open %s\n"), Prog, sgr_dbname ());
		fail_exit (1);
	}

	/*
	 * Remove /etc/gshadow entries for groups not in /etc/group.
	 */
	sgr_rewind ();
	while ((sg = sgr_next ()) != NULL) {
		if (gr_locate (sg->sg_name) != NULL) {
			continue;
		}

		if (sgr_remove (sg->sg_name) == 0) {
			/*
			 * This shouldn't happen (the entry exists) but...
			 */
			fprintf (stderr,
			         _("%s: cannot remove entry '%s' from %s\n"),
			         Prog, sg->sg_name, sgr_dbname ());
			fail_exit (3);
		}
	}

	/*
	 * Update shadow group passwords if non-shadow password is not "x".
	 * Add any missing shadow group entries.
	 */
	gr_rewind ();
	while ((gr = gr_next ()) != NULL) {
		sg = sgr_locate (gr->gr_name);
		if (NULL != sg) {
			/* update existing shadow group entry */
			sgent = *sg;
			if (strcmp (gr->gr_passwd, SHADOW_PASSWD_STRING) != 0)
				sgent.sg_passwd = gr->gr_passwd;
		} else {
			static char *empty = 0;

			/* add new shadow group entry */
			memset (&sgent, 0, sizeof sgent);
			sgent.sg_name = gr->gr_name;
			sgent.sg_passwd = gr->gr_passwd;
			sgent.sg_adm = &empty;
		}
		/*
		 * XXX - sg_mem is redundant, it is currently always a copy
		 * of gr_mem. Very few programs actually use sg_mem, and all
		 * of them are in the shadow suite. Maybe this field could
		 * be used for something else? Any suggestions?
		 */
		sgent.sg_mem = gr->gr_mem;

		if (sgr_update (&sgent) == 0) {
			fprintf (stderr,
			         _("%s: failed to prepare the new %s entry '%s'\n"),
			         Prog, sgr_dbname (), sgent.sg_name);
			fail_exit (3);
		}
		/* remove password from /etc/group */
		grent = *gr;
		grent.gr_passwd = SHADOW_PASSWD_STRING;	/* XXX warning: const */
		if (gr_update (&grent) == 0) {
			fprintf (stderr,
			         _("%s: failed to prepare the new %s entry '%s'\n"),
			         Prog, gr_dbname (), grent.gr_name);
			fail_exit (3);
		}
	}

	if (sgr_close () == 0) {
		fprintf (stderr,
		         _("%s: failure while writing changes to %s\n"),
		         Prog, sgr_dbname ());
		SYSLOG ((LOG_ERR, "failure while writing changes to %s", sgr_dbname ()));
		fail_exit (3);
	}
	if (gr_close () == 0) {
		fprintf (stderr,
		         _("%s: failure while writing changes to %s\n"),
		         Prog, gr_dbname ());
		SYSLOG ((LOG_ERR, "failure while writing changes to %s", gr_dbname ()));
		fail_exit (3);
	}
	if (sgr_unlock () == 0) {
		fprintf (stderr, _("%s: failed to unlock %s\n"), Prog, sgr_dbname ());
		SYSLOG ((LOG_ERR, "failed to unlock %s", sgr_dbname ()));
		/* continue */
	}
	sgr_locked = false;
	if (gr_unlock () == 0) {
		fprintf (stderr, _("%s: failed to unlock %s\n"), Prog, gr_dbname ());
		SYSLOG ((LOG_ERR, "failed to unlock %s", gr_dbname ()));
		/* continue */
	}
	gr_locked = false;

	nscd_flush_cache ("group");

	return 0;
}
#else				/* !SHADOWGRP */
int main (int unused(argc), char **argv)
{
	fprintf (stderr,
		 "%s: not configured for shadow group support.\n", argv[0]);
	exit (1);
}
#endif				/* !SHADOWGRP */

