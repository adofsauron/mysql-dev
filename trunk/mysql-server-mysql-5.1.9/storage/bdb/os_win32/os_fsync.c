/*-
 * See the file LICENSE for redistribution information.
 *
 * Copyright (c) 1997-2005
 *	Sleepycat Software.  All rights reserved.
 *
 * $Id: os_fsync.c,v 12.2 2005/08/10 15:47:27 bostic Exp $
 */

#include "db_config.h"

#ifndef NO_SYSTEM_INCLUDES
#include <sys/types.h>

#include <fcntl.h>			/* XXX: Required by __hp3000s900 */
#include <string.h>
#endif

#include "db_int.h"

/*
 * __os_fsync --
 *	Flush a file descriptor.
 */
int
__os_fsync(dbenv, fhp)
	DB_ENV *dbenv;
	DB_FH *fhp;
{
	int ret;

	/*
	 * Do nothing if the file descriptor has been marked as not requiring
	 * any sync to disk.
	 */
	if (F_ISSET(fhp, DB_FH_NOSYNC))
		return (0);

	if (DB_GLOBAL(j_fsync) != NULL)
		ret = DB_GLOBAL(j_fsync)(fhp->fd);
	else
		RETRY_CHK((!FlushFileBuffers(fhp->handle)), ret);

	if (ret != 0)
		__db_err(dbenv, "fsync %s", strerror(ret));
	return (ret);
}
