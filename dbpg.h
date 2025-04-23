/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The Initial Developer of the Original Code and related documentation
 * is America Online, Inc. Portions created by AOL are Copyright (C) 1999
 * America Online, Inc. All Rights Reserved.
 *
 */

/*
 * dbpg.h --
 *
 *      Private types and declarations for the nsdbpg module.
 */

#ifndef DBPG_H
#define DBPG_H

#define NSDBPG_VERSION "2.9"

/*
 * In order to obtain PG_VERSION_NUM and PG_VERSION we load the
 * pg_config.h. However, the PACKAGE_* macros conflict with
 * NaviServer's packaging information, so we drop these.
 */
#include <pg_config.h>
#undef PACKAGE_VERSION
#undef PACKAGE_TARNAME
#undef PACKAGE_STRING
#undef PACKAGE_NAME
#undef PACKAGE_BUGREPORT
#undef PACKAGE_URL

#include <nsdb.h>
#include <libpq-fe.h>

/*
 * Forward compatibility, in case a new version of the module is compiled
 * against an old version of NaviServer.
 */
#ifndef TCL_SIZE_T
# define TCL_SIZE_T           int
#endif
#ifndef TCL_OBJCMDPROC_T
# define TCL_OBJCMDPROC_T     Tcl_ObjCmdProc
# define TCL_CREATEOBJCOMMAND Tcl_CreateObjCommand
#endif
#ifndef TCL_INDEX_NONE
# define TCL_INDEX_NONE       -1
#endif

/*
 * The following structure maintains per handle data
 * specific to postgres.
 */ 

typedef struct Connection {
    PGconn         *pgconn;
    PGresult       *res;
    unsigned int    id;
    int             nCols;
    int             nTuples;
    int             curTuple;
    Ns_Time         transactionStartTime;
    bool            in_transaction;
} Connection;

extern const char *pgDbName;
extern Ns_ReturnCode Ns_PgServerInit(const char *server, const char *module, const char *driver);

#endif /* DBPG_H */

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * indent-tabs-mode: nil
 * End:
 */
