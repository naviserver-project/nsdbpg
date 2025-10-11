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
 * nsdbpg.c --
 *
 *      Implements the nsdb driver interface for the postgres database.
 */

#include "dbpg.h"

NS_EXTERN const int Ns_ModuleVersion;
NS_EXPORT const int Ns_ModuleVersion = 1;
const char *pgDbName = "PostgreSQL";

/*
 * Define a few PostgreSQL types for speed improvement (avoid UTF8
 * conversions). The types are defined in PostgreSQL in
 * <server/catalog/pg_type_d.h> which is not available in all
 * installations/versions at the same place. The used (numeric) types are
 * unchanged since a very long time and are unlikely to be changed in the
 * future.
 */
#define BOOLOID 16
#define INT2OID 21
#define INT4OID 23
#define INT8OID 20
#define OIDOID 26

/*
 * Local functions defined in this file.
 */

static const char   *DbType(Ns_DbHandle *handle);
static Ns_ReturnCode OpenDb(Ns_DbHandle *handle) NS_GNUC_NONNULL(1);
static Ns_ReturnCode CloseDb(Ns_DbHandle *handle) NS_GNUC_NONNULL(1);
static Ns_Set       *BindRow(Ns_DbHandle *handle) NS_GNUC_NONNULL(1);
static int           Exec(Ns_DbHandle *handle, const char *sql)  NS_GNUC_NONNULL(1) NS_GNUC_NONNULL(2);
static int           GetRow(const Ns_DbHandle *handle, Ns_Set *row) NS_GNUC_NONNULL(1);
static int           GetRowCount(const Ns_DbHandle *handle) NS_GNUC_NONNULL(1);
static Ns_ReturnCode Flush(const Ns_DbHandle *handle) NS_GNUC_NONNULL(1);
static Ns_ReturnCode ResetHandle(Ns_DbHandle *handle) NS_GNUC_NONNULL(1);
#if NS_VERSION_NUM >= 50000
static Tcl_Obj*      VersionInfo(Ns_DbHandle *handle) NS_GNUC_NONNULL(1);
#endif

static void SetTransactionState(const Ns_DbHandle *handle, const char *sql);

NS_EXPORT NsDb_DriverInitProc Ns_DbDriverInit;

/*
 * Local variables defined in this file.
 */

static const Ns_DbProc procs[] = {
    {DbFn_DbType,       (ns_funcptr_t)DbType},
    {DbFn_Name,         (ns_funcptr_t)DbType},
    {DbFn_OpenDb,       (ns_funcptr_t)OpenDb},
    {DbFn_CloseDb,      (ns_funcptr_t)CloseDb},
    {DbFn_BindRow,      (ns_funcptr_t)BindRow},
    {DbFn_Exec,         (ns_funcptr_t)Exec},
    {DbFn_GetRow,       (ns_funcptr_t)GetRow},
    {DbFn_GetRowCount,  (ns_funcptr_t)GetRowCount},
    {DbFn_Flush,        (ns_funcptr_t)Flush},
    {DbFn_Cancel,       (ns_funcptr_t)Flush},
    {DbFn_ResetHandle,  (ns_funcptr_t)ResetHandle},
    {DbFn_ServerInit,   (ns_funcptr_t)Ns_PgServerInit},
#if NS_VERSION_NUM >= 50000
    {DbFn_Version,      (ns_funcptr_t)VersionInfo},
#endif
    {DbFn_End, NULL}
};

static const char *dateStyle = NULL;
static unsigned int id = 0u;     /* Global count of connections. */


/*
 *----------------------------------------------------------------------
 *
 * Ns_DbDriverInit --
 *
 *      Register driver functions.
 *
 * Results:
 *      NS_OK or NS_ERROR.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

NS_EXPORT Ns_ReturnCode
Ns_DbDriverInit(const char *driver, const char *configPath)
{
    Ns_ReturnCode status;

    status = Ns_DbRegisterDriver(driver, &procs[0]);
    if (status == NS_OK) {
        const char *style = Ns_ConfigGetValue(configPath, "datestyle");

        if (style != NULL) {
            if (STRIEQ(style, "ISO") || STRIEQ(style, "SQL")
                || STRIEQ(style, "POSTGRES") || STRIEQ(style, "GERMAN")
                || STRIEQ(style, "NONEURO") || STRIEQ(style, "EURO")
                ) {
                Tcl_DString ds;

                Tcl_DStringInit(&ds);
                Ns_DStringPrintf(&ds, "set datestyle to '%s'", style);
                dateStyle = Ns_DStringExport(&ds);
                Ns_Log(Notice, "nsdbpg: Using datestyle: %s", style);
            } else {
                Ns_Log(Error, "nsdbpg: Illegal value for datestyle: %s", style);
            }
        } else {
            style = getenv("PGDATESTYLE");
            if (style != NULL) {
                Ns_Log(Notice, "nsdbpg: PGDATESTYLE in effect: %s", style);
            }
        }

#if defined(PG_VERSION_NUM) && PG_VERSION_NUM > 90100
        /*
         * PQlibVersion() was introduced in PostgreSQL 9.1
         */
        Ns_Log(Notice, "nsdbpg: version %s loaded, based on PostgreSQL %s and libbpq %d",
               NSDBPG_VERSION, PG_VERSION, PQlibVersion());
#else
        Ns_Log(Notice, "nsdbpg: version %s loaded based on PostgreSQL %s", NSDBPG_VERSION, PG_VERSION);
#endif
    }

    return status;
}

/*
 *----------------------------------------------------------------------
 *
 * ServerVersionNumber --
 *
 *      Query the database for its version string (via “SELECT version();”),
 *      parse the returned value (expected to be of the form “PostgreSQL
 *      X.Y”), and convert it into a numeric code (major * 10000 + minor).
 *
 * Results:
 *      An integer of the form (major * 10000 + minor):
 *        - >0 on success,
 *        - 0 if the query returned no row,
 *        - –1 if the version string could not be parsed.
 *
 * Side effects:
 *      Executes a one-row SQL query, allocates and uses an Ns_Set for the result,
 *      and logs the raw version string.
 *
 *----------------------------------------------------------------------
 */

#if NS_VERSION_NUM >= 50000
 static int
ServerVersionNumber(Ns_DbHandle *handle) {
    int result = 0;
    Ns_Set *rowPtr = Ns_Db1Row(handle, "SELECT version();");

    if (rowPtr != NULL) {
        int major, minor;
        /*
         * Parse version from returned string, expecting format "PostgreSQL X.Y"
         */
        if (sscanf(Ns_SetValue(rowPtr,0), "PostgreSQL %d.%d", &major, &minor) == 2) {
            /*
             * We follow the scheme of PQlibVersion where the major version is
             * multiplied by 10000 and the minor version number is added.
             */
            result = (major * 10000) + minor;
        } else {
            result = -1;
        }
        Ns_SetFree(rowPtr);
    }
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * VersionInfo --
 *
 *      Gather the PostgreSQL client and server version numbers and return
 *      them in a Tcl dictionary object.  The client version is obtained via
 *      PQlibVersion() (if available), and the server version is retrieved by
 *      querying "SELECT version();" and parsed by ServerVersionNumber().
 *
 * Results:
 *      Returns a newly created Tcl_Obj* dict containing two entries:
 *        "clientversion" : (int) client library version (or 0 if unavailable)
 *        "serverversion" : (int) server version encoded as (major * 10000 + minor),
 *                          0 if the query returned no row, or -1 on parse failure.
 *
 * Side effects:
 *      Allocates a Tcl dictionary and integer objects for the version values.
 *
 *----------------------------------------------------------------------
 */

static Tcl_Obj *
VersionInfo(Ns_DbHandle *handle)
{
    Tcl_Obj *dictObj = Tcl_NewDictObj();
    int      serverVersion = 0, clientVersion = 0;

#if defined(PG_VERSION_NUM) && PG_VERSION_NUM > 90100
    clientVersion = PQlibVersion();
#endif
    serverVersion = ServerVersionNumber(handle);

    Tcl_DictObjPut(NULL, dictObj,
                   Tcl_NewStringObj("clientversion", 13),
                   Tcl_NewIntObj(clientVersion));
    Tcl_DictObjPut(NULL, dictObj,
                   Tcl_NewStringObj("serverversion", 13),
                   Tcl_NewIntObj(serverVersion));
    return dictObj;
}
#endif


/*
 *----------------------------------------------------------------------
 *
 * DbType --
 *
 *      Return a string which identifies the driver type and name.
 *
 * Results:
 *      Database type/name.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static const char *
DbType(Ns_DbHandle *UNUSED(handle))
{
    return pgDbName;
}


/*
 *----------------------------------------------------------------------
 *
 * OpenDb --
 *
 *      Open a connection to a postgres database.  The datasource for
 *      postgres is in the form "host:port:database".
 *
 * Results:
 *      NS_OK or NS_ERROR.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
#ifdef NS_HAVE_PARSEHOST2_CONST
# define NS_PARSE_HOST_CONST const
#else
# define NS_PARSE_HOST_CONST
#endif

static Ns_ReturnCode
OpenDb(Ns_DbHandle *handle)
{
    Ns_ReturnCode status = NS_OK;

    NS_NONNULL_ASSERT(handle != NULL);

    if (handle->datasource == NULL) {
        Ns_Log(Error, "nsdbpg(%s): Invalid handle", handle->poolname);
        status = NS_ERROR;

    } else {
        Connection   *pconn;
        PGconn       *pgconn;
        Tcl_DString   ds;
        NS_PARSE_HOST_CONST char *host, *portStart = NULL;
        char         *db = NULL, *end;

        Tcl_DStringInit(&ds);
        Tcl_DStringAppend(&ds, handle->datasource, TCL_INDEX_NONE);

        Ns_HttpParseHost2(ds.string, NS_TRUE, &host, &portStart, &end);
        if (portStart != NULL) {
            db = strchr(portStart, INTCHAR(':'));
        }

        if (db == NULL) {
            Ns_Log(Error, "nsdbpg(%s): malformed datasource: \" %s\". "
                   "Should be host:port:database.",
                   handle->poolname, handle->datasource);
            status = NS_ERROR;

        } else {
            *db++ = '\0';
            Ns_Log(Notice, "nsdbpg(%s): opening connection to db %s on %s, port %s",
                   handle->poolname, db, host, portStart);
            if (STREQ(host, "localhost")) {
                pgconn = PQsetdbLogin(NULL, portStart, NULL, NULL, db, handle->user,
                                      handle->password);
            } else {
                pgconn = PQsetdbLogin(host, portStart, NULL, NULL, db, handle->user,
                                      handle->password);
            }
            if (PQstatus(pgconn) != CONNECTION_OK) {
                Ns_Log(Error, "nsdbpg(%s): could not connect to %s: %s",
                       handle->poolname, handle->datasource, PQerrorMessage(pgconn));
                PQfinish(pgconn);
                status = NS_ERROR;
            } else {
                Ns_Log(Notice, "nsdbpg(%s): opened connection to %s.",
                       handle->poolname, handle->datasource);

                pconn = ns_malloc(sizeof(Connection));
                pconn->pgconn = pgconn;
                pconn->res = NULL;
                pconn->id = id++;
                pconn->nCols = pconn->nTuples = pconn->curTuple = 0;
                pconn->in_transaction = NS_FALSE;
                handle->connection = pconn;

                if (dateStyle != NULL && Ns_DbExec(handle, dateStyle) != NS_DML) {
                    status = NS_ERROR;
                }
            }
        }
    }
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * CloseDb --
 *
 *      Close an open connection to postgres.
 *
 * Results:
 *      NS_OK or NS_ERROR.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static Ns_ReturnCode
CloseDb(Ns_DbHandle *handle) {
    Ns_ReturnCode status = NS_OK;

    NS_NONNULL_ASSERT(handle != NULL);

    if (handle->connection == NULL) {
        Ns_Log(Error, "nsdbpg(%s): invalid connection", handle->poolname);
        status = NS_ERROR;

    } else {
        Connection *pconn = handle->connection;

        Ns_Log(Ns_LogSqlDebug, "nsdbpg(%s): closing connection: %u",
               handle->poolname, pconn->id);

        PQfinish(pconn->pgconn);
        ns_free(pconn);
        handle->connection = NULL;
    }
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * BindRow --
 *
 *      Retrieve the column names of the current result.
 *
 * Results:
 *      An Ns_Set where the keys are the names of columns, or NULL on error.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static Ns_Set *
BindRow(Ns_DbHandle *handle)
{
    Ns_Set *row = NULL;

    NS_NONNULL_ASSERT(handle != NULL);

    if (handle->connection == NULL) {
        Ns_Log(Error, "nsdbpg(%s): invalid connection", handle->poolname);

    } else if (!handle->fetchingRows) {
        Ns_Log(Error, "nsdbpg(%s): No rows waiting to bind.", handle->datasource);

    } else {
        Connection *pconn = handle->connection;

        row = handle->row;
        if (PQresultStatus(pconn->res) == PGRES_TUPLES_OK) {
            int i;

            pconn->curTuple = 0;
            pconn->nCols = PQnfields(pconn->res);
            pconn->nTuples = PQntuples(pconn->res);
            row = handle->row;

            for (i = 0; i < pconn->nCols; i++) {
                /*Ns_Log(Notice, "nsdbpg(%s): col[%d] %s size %d type %d",
                       handle->datasource, i,
                       PQfname(pconn->res, i),
                       PQfsize(pconn->res, i),
                       PQftype(pconn->res, i)
                       );*/
                (void)Ns_SetPutSz(row, PQfname(pconn->res, i), -1, NULL, 0);
            }
        }
        handle->fetchingRows = NS_FALSE;
    }

    return row;
}


/*
 *----------------------------------------------------------------------
 *
 * Exec --
 *
 *      Send SQL to the database.
 *
 * Results:
 *      NS_ROWS, NS_DML or NS_ERROR.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
Exec(Ns_DbHandle *handle, const char *sql)
{
    Connection  *pconn;
    Tcl_DString  dsSql;
    int          retry_count = 2;
    int          result = NS_ERROR;

    NS_NONNULL_ASSERT(handle != NULL);
    NS_NONNULL_ASSERT(sql != NULL);

    if (*sql == '\0') {
        Ns_Log(Error, "nsdbpg(%s): No SQL query.", handle->poolname);
        return NS_ERROR;
    }
    if (handle->connection == NULL) {
        Ns_Log(Error, "nsdbpg(%s): No connection", handle->poolname);
        return NS_ERROR;
    }

    pconn = handle->connection;
    PQclear(pconn->res);

    Tcl_DStringInit(&dsSql);
    Tcl_DStringAppend(&dsSql, sql, TCL_INDEX_NONE);

    /*
     * Trim spaces
     */
    while (dsSql.length > 0 && CHARTYPE(space, dsSql.string[dsSql.length - 1]) != 0) {
        dsSql.string[--dsSql.length] = '\0';
    }
    /*
     * Make sure to end statement with a semicolon.
     */
    if (dsSql.length > 0 && dsSql.string[dsSql.length - 1] != ';') {
        Tcl_DStringAppend(&dsSql, ";", 1);
    }
    Ns_Log(Debug, "nsdbpg(%s): call PQexec with <%s>", handle->poolname, dsSql.string);
    pconn->res = PQexec(pconn->pgconn, dsSql.string);

    /*
     * Set error result for exception message -- not sure that this
     * belongs here in DRB-improved driver..... but, here it is
     * anyway, as it can't really hurt anything :-)
     */
    if (PQresultStatus(pconn->res) == PGRES_BAD_RESPONSE) {
        Tcl_DStringAppend(&handle->dsExceptionMsg, "PGRES_BAD_RESPONSE ", 19);
    }
    Tcl_DStringAppend(&handle->dsExceptionMsg,
                      PQresultErrorMessage(pconn->res), TCL_INDEX_NONE);

    /* This loop should actually be safe enough, but we'll take no
     * chances and guard against infinite retries with a counter.
     */

    while (PQstatus(pconn->pgconn) == CONNECTION_BAD && retry_count-- > 0) {

        bool in_transaction = pconn->in_transaction;

        /* Backend returns a fatal error if it really crashed.  After a crash,
         * all other backends close with a non-fatal error because shared
         * memory might've been corrupted by the crash.  In this case, we
         * will retry the query.
         */

        int retry_query = PQresultStatus(pconn->res) == PGRES_NONFATAL_ERROR;

        /* Reconnect messages need to be logged regardless of handle->verbose. */

        Ns_Log(Notice, "nsdbpg(%s): Trying to reopen database connection", handle->poolname);

        PQfinish(pconn->pgconn);

        /* We'll kick out with an NS_ERROR if we're in a transaction.
         * The queries within the transaction up to this point were
         * rolled back when the transaction crashed or closed itself
         * at the request of the postmaster.  If we were to allow the
         * rest of the transaction to continue, you'd blow transaction
         * semantics, i.e. the first portion of the transaction would've
         * rolled back and the rest of the transaction would finish its
         * inserts or whatever.  Not good!   So we return an error.  If
         * the programmer's catching transaction errors and rolling back
         * properly, there will be no problem - the rollback will be
         * flagged as occurring outside a transaction but there's no
         * harm in that.
         *
         * If the programmer's started a transaction with no "catch",
         * you'll find no sympathy on my part.
         */

        if (OpenDb(handle) == NS_ERROR || in_transaction || retry_query == 0) {
            if (in_transaction) {
                Ns_Log(Notice, "nsdbpg(%s): In transaction, conn died, error returned", handle->poolname);
            }
            Tcl_DStringFree(&dsSql);
            return NS_ERROR;
        }

        pconn = handle->connection;

        Ns_Log(Notice, "nsdbpg(%s): Retrying query", handle->poolname);
        PQclear(pconn->res);
        pconn->res = PQexec(pconn->pgconn, dsSql.string);

        /* This may again break the connection for two reasons:
         * our query may be a back-end crashing query or another
         * backend may've crashed after we reopened the backend.
         * Neither's at all likely but we'll loop back and try
         * a couple of times if it does.
         */
    }

    Tcl_DStringFree(&dsSql);

    if (pconn->res == NULL) {
        Ns_Log(Error, "nsdbpg(%s): Could not send query '%s': %s",
               handle->poolname, sql, PQerrorMessage(pconn->pgconn));
        return NS_ERROR;
    }

    /* DRB: let's clean up pgConn a bit, if someone didn't read all
     * the rows returned by a query, did a dml query, then a getrow
     * the driver might get confused if we don't zap nCols and
     * curTuple.
     */

    pconn->nCols = pconn->nTuples = pconn->curTuple = 0;

    /*
     * The driver does not make use of PIPELINE processing, so we should not
     * see the response codes for PGRES_PIPELINE_SYNC, PGRES_PIPELINE_ABORTED
     * and PGRES_TUPLES_CHUNK.
     */
    switch(PQresultStatus(pconn->res)) {
    case PGRES_TUPLES_OK:
        handle->fetchingRows = NS_TRUE;
        result = NS_ROWS;
        break;
    case PGRES_COPY_IN:
    case PGRES_COPY_OUT:
        result = NS_DML;
        break;
    case PGRES_COMMAND_OK:
        SetTransactionState(handle, sql);
        pconn->nTuples = (int)strtol(PQcmdTuples(pconn->res), NULL, 10);
        /*pconn->nTuples = PQntuples(pconn->res);*/
        result = NS_DML;
        break;
    case PGRES_BAD_RESPONSE:   NS_FALL_THROUGH; /* fall through */
    case PGRES_NONFATAL_ERROR: NS_FALL_THROUGH; /* fall through */
    case PGRES_EMPTY_QUERY:    NS_FALL_THROUGH; /* fall through */
#if defined(PG_VERSION_NUM) && PG_VERSION_NUM > 90100
    case PGRES_COPY_BOTH:      NS_FALL_THROUGH; /* fall through */
    case PGRES_SINGLE_TUPLE:   NS_FALL_THROUGH; /* fall through */
#endif
#if defined(PG_VERSION_NUM) && PG_VERSION_NUM >= 140000
    case PGRES_PIPELINE_SYNC:      NS_FALL_THROUGH; /* fall through */
    case PGRES_PIPELINE_ABORTED:   NS_FALL_THROUGH; /* fall through */
#endif
#if defined(PG_VERSION_NUM) && PG_VERSION_NUM >= 170000
    case PGRES_TUPLES_CHUNK:      NS_FALL_THROUGH; /* fall through */
#endif
    case PGRES_FATAL_ERROR:
        Ns_Log(Error, "nsdbpg(%s): result status: %d message: %s",
               handle->poolname, PQresultStatus(pconn->res), PQerrorMessage(pconn->pgconn));
        Ns_DbSetException(handle,"ERROR", PQerrorMessage(pconn->pgconn));
        result = NS_ERROR;
    }

    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * GetRow --
 *
 *      Fill in the given Ns_Set with values for each column of the
 *      current row.
 *
 * Results:
 *      NS_OK, NS_END_DATA or NS_ERROR.
 *
 * Side effects:
 *      Current tuple updated.
 *
 *----------------------------------------------------------------------
 */

static int
GetRow(const Ns_DbHandle *handle, Ns_Set *row)
{
    int          result = NS_OK;

    NS_NONNULL_ASSERT(handle != NULL);

    if (handle->connection == NULL) {
        Ns_Log(Error, "nsdbpg(%s): No connection",handle->poolname);
        result = NS_ERROR;

    } else if (row == NULL) {
        Ns_Log(Error, "nsdbpg(%s): Invalid Ns_Set -> row", handle->poolname);
        result = NS_ERROR;

    } else {
        Connection *pconn = handle->connection;

        if (pconn->nCols == 0) {
            Ns_Log(Error, "nsdbpg(%s): GetRow called outside a fetch row loop.",
                   handle->poolname);
            result = NS_ERROR;

        } else if (pconn->curTuple == pconn->nTuples) {
            PQclear(pconn->res);
            pconn->res = NULL;
            pconn->nCols = pconn->nTuples = pconn->curTuple = 0;
            result = NS_END_DATA;

        } else {
            size_t i;

            Ns_SetClearValues(row, 4096);

            for (i = 0u; i < (size_t)pconn->nCols; i++) {
                Tcl_DString ds, *dsPtr = &ds;
                int   rawFieldLength = PQgetlength(pconn->res, pconn->curTuple, (int)i);
                char *rawFieldValue  = PQgetvalue(pconn->res, pconn->curTuple, (int)i);
                Oid   pgType         = PQftype(pconn->res, (int)i);

                /*Ns_Log(Notice, "nsdbpg(%s): GetRow %lu type %u '%s' PQgetlength %d",
                  handle->poolname, i,  pgType, rawFieldValue, rawFieldLength);*/

                /*
                 * Avoid for some common datatypes the UTF8 conversion for
                 * speed improvement. The types are defined in PostgreSQL in
                 * <server/catalog/pg_type_d.h> which is not available in all
                 * installations/versions at the same place. The used
                 * (numeric) types unchanged since a very long time and are
                 * unlikely to be changed in the future.
                 */
                switch (pgType) {
                case BOOLOID: NS_FALL_THROUGH; /* fall through */
                case INT2OID: NS_FALL_THROUGH; /* fall through */
                case INT4OID: NS_FALL_THROUGH; /* fall through */
                case INT8OID: NS_FALL_THROUGH; /* fall through */
                case OIDOID:
                    Ns_SetPutValueSz(row, i, rawFieldValue, rawFieldLength);
                    break;

                default:
                    /*
                     * Tcl_ExternalToUtfDString performs by itself a
                     * Tcl_DStringInit(dsPtr); passing a dsPtr with preallocated
                     * size causes a memory leak (e.g., when using the usual idiom
                     * setting the length of the dsPtr to 0 to keep the
                     * preallocated dynamic memory).
                     */
                    (void)Tcl_ExternalToUtfDString(NULL, rawFieldValue, (TCL_SIZE_T)rawFieldLength, dsPtr);
                    /*Ns_Log(Notice, "nsdbpg(%s): GetRow %lu converted '%s' PQgetlength %d",
                      handle->poolname, i, dsPtr->string, dsPtr->length);*/
                    Ns_SetPutValueSz(row, i, dsPtr->string, dsPtr->length);
                    Tcl_DStringFree(dsPtr);
                    break;
                }
            }

#ifdef NS_SET_DEBUG
            Ns_Log(Notice, "nsdbpg(%s): GetRow set %p size %lu maxsize %lu data len %d avail %d DONE",
                   handle->poolname,
                   (void*)row, row->size, row->maxSize,
                   row->data.length, row->data.spaceAvl);
#endif
            pconn->curTuple++;
        }
    }
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * GetRowCount --
 *
 *      Returns number of rows processed by the last SQL statement
 *
 * Results:
 *      Number of rows or NS_ERROR.
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

static int
GetRowCount(const Ns_DbHandle *handle)
{
    int result;

    NS_NONNULL_ASSERT(handle != NULL);

    if (handle->connection == NULL) {
        Ns_Log(Error, "nsdbpg(%s): No connection", handle->poolname);
        result = (int)NS_ERROR;
    } else {
        const Connection *pconn = handle->connection;
        result = pconn->nTuples;
    }
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * Flush --
 *
 *      Flush unfetched rows.
 *
 * Results:
 *      NS_OK or NS_ERROR.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static Ns_ReturnCode
Flush(const Ns_DbHandle *handle)
{
    Ns_ReturnCode status = NS_OK;

    NS_NONNULL_ASSERT(handle != NULL);

    if (handle->connection == NULL) {
        Ns_Log(Error, "nsdbpg(%s): Invalid connection", handle->poolname);
        status = NS_ERROR;

    } else {
        Connection *pconn = handle->connection;

        if (pconn->nCols > 0) {
            PQclear(pconn->res);
            pconn->res = NULL;
            pconn->nCols = pconn->nTuples = pconn->curTuple = 0;
        }
    }
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * ResetHandle --
 *
 *      Reset connection ready for next command.
 *
 * Results:
 *      NS_OK or NS_ERROR.
 *
 * Side effects:
 *      Any active transaction will be rolled back.
 *
 *----------------------------------------------------------------------
 */

static Ns_ReturnCode
ResetHandle(Ns_DbHandle *handle)
{
    Ns_ReturnCode status = NS_OK;

    NS_NONNULL_ASSERT(handle != NULL);

    if (handle->connection == NULL) {
        Ns_Log(Error, "nsdbpg(%s): Invalid connection", handle->poolname);
        status = NS_ERROR;

    } else {
        const Connection *pconn = handle->connection;

        if (pconn->in_transaction) {
            if (handle->verbose) {
                Ns_Log(Ns_LogSqlDebug, "nsdbpg(%s): Rolling back transaction", handle->poolname);
            }
            if (Ns_DbExec(handle, "rollback") != (int)PGRES_COMMAND_OK) {
                Ns_Log(Error, "nsdbpg(%s): Rollback failed", handle->poolname);
            }
            status = NS_ERROR;
        }
    }

    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * SetTransactionState --
 *
 *      Set the current transaction state based on the query pointed
 *      to by "sql".  Should be called only after the query has
 *      successfully been executed.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static void
SetTransactionState(const Ns_DbHandle *handle, const char *sql)
{
    Connection *pconn = handle->connection;
    bool        transactionFinished = NS_FALSE;
    const char *reason;

    while (*sql == ' ') {
        sql++;
    }
    if (strncasecmp(sql, "begin", 5u) == 0) {
        pconn->in_transaction = NS_TRUE;
        if (handle->verbose) {
            Ns_Log(Ns_LogSqlDebug, "nsdbpg(%s): Entering transaction.", handle->poolname);
        }
        reason = "started";
        Ns_GetTime(&pconn->transactionStartTime);
    } else if (strncasecmp(sql, "end", 3u) == 0
               || strncasecmp(sql, "commit", 6u) == 0) {
        pconn->in_transaction = NS_FALSE;
        if (handle->verbose) {
            Ns_Log(Ns_LogSqlDebug, "nsdbpg(%s): Committing transaction.", handle->poolname);
        }
        transactionFinished = NS_TRUE;
        reason = "committed";

    } else if (strncasecmp(sql, "abort", 5u) == 0
               || strncasecmp(sql, "rollback", 8u) == 0) {
        pconn->in_transaction = NS_FALSE;
        if (handle->verbose) {
            Ns_Log(Ns_LogSqlDebug, "nsdbpg(%s): Rolling back transaction.", handle->poolname);
        }
        transactionFinished = NS_TRUE;
        reason = "aborted";
    }

    if (transactionFinished) {
        Ns_Time endTime, diffTime;

        Ns_GetTime(&endTime);
        (void)Ns_DiffTime(&endTime, &pconn->transactionStartTime, &diffTime);

        /*
         * Log entry, when SQL debug is enabled and SQL time is above
         * logging threshold.
         */
        if (Ns_LogSeverityEnabled(Ns_LogSqlDebug) == NS_TRUE) {
            Ns_Time *minDurationPtr;

            if (Ns_DbGetMinDuration(NULL, handle->poolname, &minDurationPtr) == TCL_OK) {
                long delta = Ns_DiffTime(minDurationPtr, &diffTime, NULL);

                if (delta < 1) {
                    Ns_Log(Ns_LogSqlDebug, "pool %s duration " NS_TIME_FMT " secs: transaction %s",
                           handle->poolname, (int64_t)diffTime.sec, diffTime.usec, reason);
                }
            }
        }
    }
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * indent-tabs-mode: nil
 * End:
 */
