/*
 Copyright (c) 2002 Aaron Stone, aaron@serendipity.cx

 This program is free software; you can redistribute it and/or 
 modify it under the terms of the GNU General Public License 
 as published by the Free Software Foundation; either 
 version 2 of the License, or (at your option) any later 
 version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/*
 * $Id$
 * * User authentication functions for LDAP.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "assert.h"
#include "auth.h"
#include "dbmail.h"
#include "db.h"
#include "dbmd5.h"
#include "debug.h"
#include "list.h"
#include "misc.h"
#include <ldap.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
//#include <crypt.h>
#include <time.h>

#define AUTH_QUERY_SIZE 1024

static char *configFile = DEFAULT_CONFIG_FILE;

LDAP *_ldap_conn;
LDAPMod **_ldap_mod;
LDAPMessage *_ldap_res;
LDAPMessage *_ldap_msg;
int _ldap_err;
int _ldap_attrsonly = 0;
char *_ldap_dn;
char **_ldap_vals;
char **_ldap_attrs = NULL;
char _ldap_query[AUTH_QUERY_SIZE]; 

typedef struct _ldap_cfg {
  field_t bind_dn,
    bind_pw,
    base_dn,
    port,
    scope,
    hostname,
    objectclass;
  field_t field_uid,
    field_cid,
    field_nid,
    field_mail,
    field_mailalt,
    mailaltprefix,
    field_maxmail,
    field_passwd,
    field_fwd,
    field_fwdsave,
    field_fwdtarget,
    fwdtargetprefix,
    field_members;
  int scope_int,
    port_int;
} _ldap_cfg_t;

_ldap_cfg_t _ldap_cfg;

/* Define a macro to cut down on code duplication... */
#define GETCONFIGVALUE( func, list, val, var )	\
	GetConfigValue(val, list, var);		\
	if (strlen(var) == 0)			\
		trace(TRACE_DEBUG, #func ": no value for " #val " in config file");	\
	trace(TRACE_DEBUG, #func ": value for " #val " stored in " #var " as [%s]", var)
	/* that's correct, no final ; so when the macro is called, it looks "normal" */

static void __auth_get_config(void);

static void __auth_get_config()
{
  struct list ldapItems;

  ReadConfig("LDAP", configFile, &ldapItems);
  SetTraceLevel(&ldapItems);

  GETCONFIGVALUE(__auth_get_config(), &ldapItems, "BIND_DN",	 _ldap_cfg.bind_dn	);
  GETCONFIGVALUE(__auth_get_config(), &ldapItems, "BIND_PW",	 _ldap_cfg.bind_pw	);
  GETCONFIGVALUE(__auth_get_config(), &ldapItems, "BASE_DN",	 _ldap_cfg.base_dn	);
  GETCONFIGVALUE(__auth_get_config(), &ldapItems, "PORT",	 _ldap_cfg.port		);
  GETCONFIGVALUE(__auth_get_config(), &ldapItems, "HOSTNAME",	 _ldap_cfg.hostname	);
  GETCONFIGVALUE(__auth_get_config(), &ldapItems, "OBJECTCLASS", _ldap_cfg.objectclass	);
  GETCONFIGVALUE(__auth_get_config(), &ldapItems, "FIELD_UID",	 _ldap_cfg.field_uid	);
  GETCONFIGVALUE(__auth_get_config(), &ldapItems, "FIELD_CID",	 _ldap_cfg.field_cid	);
  GETCONFIGVALUE(__auth_get_config(), &ldapItems, "FIELD_NID",	 _ldap_cfg.field_nid	);
  GETCONFIGVALUE(__auth_get_config(), &ldapItems, "FIELD_MAIL",	 _ldap_cfg.field_mail	);
  GETCONFIGVALUE(__auth_get_config(), &ldapItems, "FIELD_MAILALT",_ldap_cfg.field_mailalt);
  GETCONFIGVALUE(__auth_get_config(), &ldapItems, "MAILALTPREFIX",_ldap_cfg.mailaltprefix);
  GETCONFIGVALUE(__auth_get_config(), &ldapItems, "FIELD_QUOTA", _ldap_cfg.field_maxmail);
  GETCONFIGVALUE(__auth_get_config(), &ldapItems, "FIELD_PASSWD", _ldap_cfg.field_passwd);
  GETCONFIGVALUE(__auth_get_config(), &ldapItems, "FIELD_FORWARD", _ldap_cfg.field_fwd	);
  GETCONFIGVALUE(__auth_get_config(), &ldapItems, "FIELD_FWDSAVE", _ldap_cfg.field_fwdsave);
  GETCONFIGVALUE(__auth_get_config(), &ldapItems, "FIELD_FWDTARGET", _ldap_cfg.field_fwdtarget);
  GETCONFIGVALUE(__auth_get_config(), &ldapItems, "FWDTARGETPREFIX", _ldap_cfg.fwdtargetprefix);
  GETCONFIGVALUE(__auth_get_config(), &ldapItems, "FIELD_MEMBERS", _ldap_cfg.field_members);
  GETCONFIGVALUE(__auth_get_config(), &ldapItems, "SCOPE",	 _ldap_cfg.scope	);

  /* Store the port as an integer for later use. */
  _ldap_cfg.port_int = atoi( _ldap_cfg.port );

  /* Compare the input string with the possible options,
   * making sure not to exceeed the length of the given string */
  {
    int len = ( strlen( _ldap_cfg.scope ) < 3 ? strlen( _ldap_cfg.scope ) : 3 );

    if( strncasecmp( _ldap_cfg.scope, "one", len )  == 0 )
      _ldap_cfg.scope_int = LDAP_SCOPE_ONELEVEL;
    else if( strncasecmp( _ldap_cfg.scope, "bas", len ) == 0 )
      _ldap_cfg.scope_int = LDAP_SCOPE_BASE;
    else if( strncasecmp( _ldap_cfg.scope, "sub", len ) == 0 )
      _ldap_cfg.scope_int = LDAP_SCOPE_SUBTREE;
    else
      _ldap_cfg.scope_int = LDAP_SCOPE_SUBTREE;
  }
  trace(TRACE_DEBUG, "__auth_get_config(): integer ldap scope is [%d]", _ldap_cfg.scope_int);

  list_freelist( &ldapItems.start );
}

/*
 * auth_connect()
 *
 * initializes the connection for authentication.
 * 
 * returns 0 on success, -1 on failure
 */
int auth_connect()
{
  __auth_get_config();
  return 0;
}

int auth_disconnect()
{
  /* Destroy the connection */
  if( _ldap_conn != NULL )
    {
      trace(TRACE_DEBUG, "auth_disconnect(): disconnecting from ldap server" );
      ldap_unbind( _ldap_conn );
    }
  else
    {
      trace(TRACE_DEBUG, "auth_disconnect(): was already disconnected from ldap server" );
    }
  return 0;
}

/*
 * At the top of each function, rebind to the server
 *
 * Someday, this will be smart enough to know if the
 * connection has a problem, and only then will it
 * do the unbind->init->bind dance.
 *
 * For now, we are lazy and resource intensive! Why?
 * Because we leave the connection open to lag to death
 * at the end of each function. It's a trade off, really.
 * We could always close it at the end, but then we'd
 * never be able to recycle a connection for a flurry of
 * calls. OTOH, if the calls are always far between, we'd
 * rather just be connected strictly as needed...
 */
int auth_reconnect(void);
int auth_reconnect()
{
  /* Destroy the old... */
  if( _ldap_conn != NULL )
    {
      trace(TRACE_DEBUG, "auth_reconnect(): disconnecting from ldap server" );
      ldap_unbind( _ldap_conn );
    }
  else
    {
      trace(TRACE_DEBUG, "auth_reconnect(): was already disconnected from ldap server" );
    }

  /* ...and make anew! */
  trace(TRACE_DEBUG, "auth_reconnect(): connecting to ldap server on [%s] : [%d]", _ldap_cfg.hostname, _ldap_cfg.port_int );
  _ldap_conn = ldap_init( _ldap_cfg.hostname, _ldap_cfg.port_int );
  trace(TRACE_DEBUG, "auth_reconnect(): binding to ldap server as [%s] / [%s]", _ldap_cfg.bind_dn, _ldap_cfg.bind_pw );
  _ldap_err = ldap_bind_s( _ldap_conn, _ldap_cfg.bind_dn, _ldap_cfg.bind_pw, LDAP_AUTH_SIMPLE );
  if( _ldap_err )
    {
      trace(TRACE_ERROR,"auth_reconnect(): ldap_bind_s failed: %s",
	    ldap_err2string( _ldap_err ) );
      return -1;
    }

  trace(TRACE_DEBUG, "auth_reconnect(): successfully bound to ldap server");
  return 0;
}

/*
int __auth_add(const char *q)
{
  LDAP *__ldap_conn;
  LDAPMod **__ldap_mod;
  LDAPMessage *__ldap_res;
  LDAPMessage *__ldap_msg;
  int __ldap_err;
  int __ldap_attrsonly = 0;
  char *__ldap_dn;
  char **__ldap_vals;
  char **__ldap_attrs = NULL;
  char __ldap_query[AUTH_QUERY_SIZE]; 

  if (!q)
    {
      trace(TRACE_ERROR, "__auth_query(): got NULL query");
      return 0;
    }
}
*/
/*
 * The list that goes into retlist is really big and scary.
 * Here's how it works...
 *
 * Each node of retlist contains a data field
 * which is a pointer to another list, "fieldlist".
 *
 * Each node of fieldlist contains a data field
 * which is a pointer to another list, "datalist".
 *
 * Each node of datalist contains a data field
 * which is a (char *) pointer to some actual data.
 *
 * Here's a visualization:
 *
 * retlist
 *  has the "rows" that matched
 *   {
 *     (struct list *)data
 *       has the fields you requested
 *       {
 *         (struct list *)data
 *           has the values for the field
 *           {
 *             (char *)data
 *             (char *)data
 *             (char *)data
 *           }
 *       }
 *   }
 *
 * */

/* returns the number of matches found */
int __auth_get_one_entry(const char *q, char **retfields, struct list *retlist)
{
  LDAPMessage *ldap_res;
  LDAPMessage *ldap_msg;
  int ldap_err;
  int ldap_attrsonly = 0;
  /* char *ldap_dn; this variable is unused */
  char **ldap_vals;
  char **ldap_attrs = NULL;
  char ldap_query[AUTH_QUERY_SIZE]; 
  int i = 0, j = 0, k = 0, m = 0;
  struct list fieldlist, datalist;

  if (!q)
    {
      trace(TRACE_ERROR, "__auth_get_one_entry(): got NULL query");
      goto endnofree;
    }

  auth_reconnect();

  snprintf( ldap_query, AUTH_QUERY_SIZE, "%s", q );
  trace(TRACE_DEBUG, "__auth_get_one_entry(): retrieving entry for DN [%s]", ldap_query );
  ldap_err = ldap_search_s( _ldap_conn, ldap_query, LDAP_SCOPE_BASE, "(objectClass=*)", ldap_attrs, ldap_attrsonly, &ldap_res );
  if ( ldap_err )
    {
      trace(TRACE_ERROR, "__auth_get_one_entry(): could not retrieve DN: %s", ldap_err2string( ldap_err ) );
      goto endnofree;
    }

  /* we're just using a little counter variable,
   * since we'll use it in the for loop later */
  j = ldap_count_entries( _ldap_conn, ldap_res );

  if ( j < 1 ) 
    {
      trace (TRACE_DEBUG,"__auth_get_one_entry(): none found");
      goto endnofree; 
    } 

  /* do the first entry here */
  ldap_msg = ldap_first_entry( _ldap_conn, ldap_res );
  if ( ldap_msg == NULL )
    {
      ldap_get_option(_ldap_conn, LDAP_OPT_ERROR_NUMBER, &_ldap_err);
      trace(TRACE_ERROR,"__auth_get_one_entry(): ldap_first_entry failed: %s", ldap_err2string( _ldap_err ) );
      goto endnofree;
    }

  list_init( retlist );

  /* we'll get the next entry at the _end_ of the loop! */
  /* get the entries to populate retlist */
  for ( i = 0; i < j; i++ )
    {
      /* init this list for the field values */
      list_init( &fieldlist );

      /* get the fields to populate fieldlist */
      for ( k = 0; retfields[k] != NULL; k++ )
        {
          /* init this list for the data values */
	  list_init( &datalist );

          /* get the values to populate datalist */
          ldap_vals = ldap_get_values( _ldap_conn, ldap_msg, retfields[k] );
          if ( ldap_vals == NULL )
            {
              ldap_get_option(_ldap_conn, LDAP_OPT_ERROR_NUMBER, &ldap_err);
              trace(TRACE_ERROR,"__auth_get_one_entry(): ldap_get_values failed: %s", ldap_err2string( ldap_err ) );
              /* no need to break, because we WANT the list to contain an entry
	       * for each attribute, even if it is simply the freshly-initialized
	       * list, which has no nodes -- that's just fine by us and our consumers
	      break;
	       */
            }
	  else {
          for ( m = 0; ldap_vals[m] != NULL; m++ )
            {
              /* add the value to the list */
              if ( !list_nodeadd( &datalist, ldap_vals[m], strlen( ldap_vals[m] ) + 1 ) )
                {
		  trace(TRACE_ERROR,  "__auth_get_one_entry: could not add ldap_vals to &datalist" );
                  list_freelist( &datalist.start );
                  break;
                }
	    }
	  }
          /* add the value to the list */
          if ( !list_nodeadd( &fieldlist, &datalist, sizeof( struct list ) ) )
            {
              trace(TRACE_ERROR,  "__auth_get_one_entry(): could not add &datalist to &fieldlist" );
              list_freelist( &fieldlist.start );
              break;
            }
          /* free the values as we use them */
          ldap_value_free( ldap_vals );
	}
      /* add the value to the list */
      if ( !list_nodeadd( retlist, &fieldlist, sizeof( struct list ) ) )
        {
          trace(TRACE_ERROR, "__auth_get_one_entry(): could not add &fieldlist to retlist" );
          list_freelist( &retlist->start );
          goto endfree;
        }

      /* do the next entry here */
      ldap_msg = ldap_next_entry( _ldap_conn, ldap_msg );
      if ( ldap_msg == NULL )
        {
          ldap_get_option(_ldap_conn, LDAP_OPT_ERROR_NUMBER, &ldap_err);
          trace(TRACE_ERROR,"__auth_get_one_entry(): ldap_next_entry failed: %s", ldap_err2string( ldap_err ) );
          //goto endfree;
	  break;
        }
    }

  endfree:
  if( ldap_res ) ldap_msgfree( ldap_res );

  endnofree:
  return j; /* remember, j = ldap_count_entries() */
}

/* returns the number of matches found */
int __auth_get_every_match(const char *q, char **retfields, struct list *retlist)
{
  LDAPMessage *ldap_res;
  LDAPMessage *ldap_msg;
  int ldap_err;
  int ldap_attrsonly = 0;
  /* char *ldap_dn; usunused variable */
  char **ldap_vals;
  char **ldap_attrs = NULL;
  char ldap_query[AUTH_QUERY_SIZE]; 
  int i = 0, j = 0, k = 0, m = 0;
  struct list fieldlist, datalist;

  if (!q)
    {
      trace(TRACE_ERROR, "__auth_get_every_match(): got NULL query");
      goto endnofree;
    }

  auth_reconnect();

  snprintf( ldap_query, AUTH_QUERY_SIZE, "%s", q );
  trace(TRACE_DEBUG, "__auth_get_every_match(): searching with query [%s]", ldap_query );
  ldap_err = ldap_search_s( _ldap_conn, _ldap_cfg.base_dn, _ldap_cfg.scope_int, ldap_query, ldap_attrs, ldap_attrsonly, &ldap_res );
  if ( ldap_err )
    {
      trace(TRACE_ERROR, "__auth_get_every_match(): could not execute query: %s", ldap_err2string( ldap_err ) );
      if( ldap_res != NULL ) ldap_msgfree( ldap_res );
      goto endnofree;
    }

  /* we're just using a little counter variable,
   * since we'll use it in the for loop later */
  j = ldap_count_entries( _ldap_conn, ldap_res );

  if ( j < 1 ) 
    {
      trace (TRACE_DEBUG,"__auth_get_every_match(): none found");
      if( ldap_res != NULL ) ldap_msgfree( ldap_res );
      goto endnofree; 
    } 

  /* do the first entry here */
  ldap_msg = ldap_first_entry( _ldap_conn, ldap_res );
  if ( ldap_msg == NULL )
    {
      ldap_get_option(_ldap_conn, LDAP_OPT_ERROR_NUMBER, &_ldap_err);
      trace(TRACE_ERROR,"__auth_get_every_match(): ldap_first_entry failed: %s", ldap_err2string( _ldap_err ) );
      if( ldap_res != NULL ) ldap_msgfree( ldap_res );
      goto endnofree;
    }

  list_init( retlist );

  /* we'll get the next entry at the _end_ of the loop! */
  /* get the entries to populate retlist */
  for ( i = 0; i < j; i++ )
    {
      /* init this list for the field values */
      list_init( &fieldlist );

      /* get the fields to populate fieldlist */
      for ( k = 0; retfields[k] != NULL; k++ )
        {
          /* init this list for the data values */
	  list_init( &datalist );

          /* get the values to populate datalist */
          ldap_vals = ldap_get_values( _ldap_conn, ldap_msg, retfields[k] );
          if ( ldap_vals == NULL )
            {
              ldap_get_option(_ldap_conn, LDAP_OPT_ERROR_NUMBER, &ldap_err);
              trace(TRACE_ERROR,"__auth_get_every_match(): ldap_get_values failed: %s", ldap_err2string( ldap_err ) );
              /* no need to break, because we WANT the list to contain an entry
	       * for each attribute, even if it is simply the freshly-initialized
	       * list, which has no nodes -- that's just fine by us and our consumers
	      break;
	       */
            }
	  else {
          for ( m = 0; ldap_vals[m] != NULL; m++ )
            {
              /* add the value to the list */
              if ( !list_nodeadd( &datalist, ldap_vals[m], strlen( ldap_vals[m] ) + 1 ) )
                {
		  trace(TRACE_ERROR, "__auth_get_every_match: could not add ldap_vals to &datalist" );
                  list_freelist( &datalist.start );
                  break;
                }
	    }
	  }
          /* add the value to the list */
          if ( !list_nodeadd( &fieldlist, &datalist, sizeof( struct list ) ) )
            {
              trace(TRACE_ERROR, "__auth_get_every_match(): could not add &datalist to &fieldlist" );
              list_freelist( &fieldlist.start );
              break;
            }
          /* free the values as we use them */
          ldap_value_free( ldap_vals );
	}
      /* add the value to the list */
      if ( !list_nodeadd( retlist, &fieldlist, sizeof( struct list ) ) )
        {
          trace(TRACE_ERROR, "__auth_get_every_match(): could not add &fieldlist to retlist" );
          list_freelist( &retlist->start );
          goto endfree;
        }

      /* do the next entry here */
      ldap_msg = ldap_next_entry( _ldap_conn, ldap_msg );
      if ( ldap_msg == NULL )
        {
          ldap_get_option(_ldap_conn, LDAP_OPT_ERROR_NUMBER, &ldap_err);
          trace(TRACE_ERROR,"__auth_get_every_match(): ldap_next_entry failed: %s", ldap_err2string( ldap_err ) );
          //goto endfree;
	  break;
        }
    }

  endfree:
  if( ldap_res ) ldap_msgfree( ldap_res );
  if( ldap_msg ) ldap_msgfree( ldap_msg );

  endnofree:
  return j; /* remember, j = ldap_count_entries() */
}

char *__auth_get_first_match(const char *q, char **retfields)
{
  LDAPMessage *ldap_res;
  LDAPMessage *ldap_msg;
  int ldap_err;
  int ldap_attrsonly = 0;
  char *ldap_dn = NULL;
  char **ldap_vals = NULL;
  char **ldap_attrs = NULL;
  char ldap_query[AUTH_QUERY_SIZE]; 
  int k = 0;
  char *returnid = NULL;

  if (!q)
    {
      trace(TRACE_ERROR, "__auth_get_first_match(): got NULL query");
      goto endnofree;
    }

  auth_reconnect();

  snprintf( ldap_query, AUTH_QUERY_SIZE, "%s", q );
  trace(TRACE_DEBUG, "__auth_get_first_match(): searching with query [%s]", ldap_query );
  ldap_err = ldap_search_s( _ldap_conn, _ldap_cfg.base_dn, _ldap_cfg.scope_int, ldap_query, ldap_attrs, ldap_attrsonly, &ldap_res );
  if ( ldap_err )
    {
      trace(TRACE_ERROR, "__auth_get_first_match(): could not execute query: %s", ldap_err2string( ldap_err ) );
      goto endfree;
    }

  if ( ldap_count_entries( _ldap_conn, ldap_res ) < 1 ) 
    {
      trace (TRACE_DEBUG,"__auth_get_first_match(): none found");
      goto endfree;
    } 

  ldap_msg = ldap_first_entry( _ldap_conn, ldap_res );
  if ( ldap_msg == NULL )
    {
      ldap_get_option(_ldap_conn, LDAP_OPT_ERROR_NUMBER, &ldap_err);
      trace(TRACE_ERROR,"__auth_get_first_match(): ldap_first_entry failed: %s", ldap_err2string( ldap_err ) );
      goto endfree;
    }

  for ( k = 0; retfields[k] != NULL; k++ )
    {
      ldap_vals = ldap_get_values( _ldap_conn, ldap_msg, retfields[k] );
      //FIXME: something about collecting up the values...
      if( 0 == strcasecmp( retfields[k], "dn" ) )
        {
          ldap_dn = ldap_get_dn( _ldap_conn, ldap_msg );
          if ( ldap_dn )
            {
              if ( !( returnid = (char *)my_malloc( strlen( ldap_dn ) + 1 ) ) )
                {
                  trace( TRACE_ERROR, "__auth_get_first_match(): out of memory" );
                  goto endfree;
                }
                  
              /* this is safe because we calculated the size three lines ago */
              strncpy( returnid, ldap_dn, strlen(ldap_dn));
            }
        }
      else
        {
          ldap_vals = ldap_get_values( _ldap_conn, ldap_msg, retfields[k] );
          if ( ldap_vals )
            {
              if ( !( returnid = (char *)my_malloc( strlen( ldap_vals[0] ) + 1 ) ) )
                {
                  trace( TRACE_ERROR, "__auth_get_first_match(): out of memory" );
                  goto endfree;
                }
                  
              /* this is safe because we calculated the size three lines ago */
              strncpy( returnid, ldap_vals[0], strlen(ldap_vals[0]) );
            }
        }
    }

  endfree:
  if( !(NULL == ldap_dn) ) ldap_memfree( ldap_dn );
  if( !(NULL == ldap_vals) ) ldap_value_free( ldap_vals );
 // if( !((LDAPMessage *)0 == ldap_res) ) ldap_msgfree( ldap_res );
  if( !((LDAPMessage *)0 == ldap_res) ) ldap_msgfree( ldap_res );

  endnofree:
  return returnid;
}


int auth_user_exists(const char *username, u64_t *user_idnr)
{
  char *id_char;
  char query[AUTH_QUERY_SIZE];
  char *fields[] = { _ldap_cfg.field_nid, NULL };

  assert(user_idnr != NULL);
  *user_idnr = 0;
  
  if (!username)
  {
       trace(TRACE_ERROR,"auth_user_exists(): got NULL as username");
       return 0;
  }

  snprintf( query, AUTH_QUERY_SIZE, "(%s=%s)", _ldap_cfg.field_uid, username );
  id_char = __auth_get_first_match( query, fields );

  *user_idnr = ( id_char ) ? strtoull( id_char, NULL, 0 ) : 0;
  trace(TRACE_DEBUG, "auth_user_exists(): returned value is [%llu]", user_idnr );

  if( id_char ) free( id_char );
  
  if (*user_idnr == 0)
       return 0;
  else
       return 1;
}

/* Given a useridnr, find the account/login name
 * return 0 if not found, NULL on error
 */
char *auth_get_userid (u64_t user_idnr)
{
  char *returnid = NULL;
  char query[AUTH_QUERY_SIZE];
  char *fields[] = { _ldap_cfg.field_uid, NULL };
  /*
  if (!user_idnr)
    {
      trace(TRACE_ERROR,"auth_get_userid(): got NULL as useridnr");
      return 0;
    }
  */
  snprintf(query, AUTH_QUERY_SIZE, "(%s=%llu)", _ldap_cfg.field_nid, user_idnr );
  returnid = __auth_get_first_match( query, fields );

  trace(TRACE_DEBUG, "auth_getuserid(): returned value is [%s]", returnid );

  return returnid;
}


/*
 * Get the Client ID number
 * Return 0 on successful failure
 * Return -1 on really big failures
 */
int auth_getclientid(u64_t user_idnr, u64_t *client_idnr)
{
  char *cid_char = NULL;
  char query[AUTH_QUERY_SIZE];
  char *fields[] = { _ldap_cfg.field_cid, NULL };

  assert (client_idnr != NULL);
  *client_idnr = 0;

  if (!user_idnr)
    {
      trace(TRACE_ERROR,"auth_getclientid(): got NULL as useridnr");
      return -1;
    }

  snprintf( query, AUTH_QUERY_SIZE, "(%s=%llu)", _ldap_cfg.field_nid, user_idnr );
  cid_char = __auth_get_first_match( query, fields );

  *client_idnr = ( cid_char ) ? strtoull( cid_char, NULL, 0 ) : 0;
  trace(TRACE_DEBUG, "auth_getclientid(): returned value is [%llu]", client_idnr );

  if( cid_char ) free( cid_char );

  return 1;
}


int auth_getmaxmailsize(u64_t user_idnr, u64_t *maxmail_size)
{
  char *max_char;
  char query[AUTH_QUERY_SIZE];
  char *fields[] = { _ldap_cfg.field_cid, NULL };

  assert(maxmail_size != NULL);
  *maxmail_size = 0;

  if (!user_idnr)
    {
      trace(TRACE_ERROR,"auth_getmaxmailsize(): got NULL as useridnr");
      return 0;
    }

  snprintf( query, AUTH_QUERY_SIZE, "(%s=%llu)", _ldap_cfg.field_nid, user_idnr );
  max_char = __auth_get_first_match( query, fields );

  *maxmail_size = ( max_char ) ? strtoull( max_char, 0, 10 ) : 0;
  trace(TRACE_DEBUG, "auth_getmaxmailsize(): returned value is [%llu]", 
	maxmail_size);

  if( max_char ) free( max_char );

  return 1;
}


/*
 * auth_getencryption()
 *
 * returns a string describing the encryption used for the passwd storage
 * for this user.
 * The string is valid until the next function call; in absence of any 
 * encryption the string will be empty (not null).
 *
 * If the specified user does not exist an empty string will be returned.
 */
char *auth_getencryption(u64_t user_idnr UNUSED)
{
  /* ldap does not support fancy passwords */
  return 0;
}

/* Fills the users list with all existing users
 * return -2 on mem error, -1 on db-error, 0 on success */
int auth_get_known_users(struct list *users)
{
  int64_t known;
  /* u64_t curr; unused variable */
  char query[AUTH_QUERY_SIZE];
  char *fields[] = { _ldap_cfg.field_uid, NULL };
  struct list templist;
  struct element *tempelem1, *tempelem2, *tempelem3;

  if (!users)
    {
      trace(TRACE_ERROR,"auth_get_known_users(): got a NULL pointer as argument");
      return -2;
    }

  list_init(users);

  snprintf( query, AUTH_QUERY_SIZE, "(objectClass=%s)", _ldap_cfg.objectclass );
  known = __auth_get_every_match( query, fields, &templist );
  trace(TRACE_ERROR,"auth_get_known_users(): found %llu users", known );

  /* do the first entry here */
  tempelem1 = list_getstart( &templist );

  /* we'll get the next entry at the _end_ of the loop! */
  while( tempelem1 != NULL )
    {
      tempelem2 = list_getstart( (struct list *)tempelem1->data );
      while( tempelem2 != NULL )
        {
          tempelem3 = list_getstart( (struct list *)tempelem2->data );
          while( tempelem3 != NULL )
            {
              list_nodeadd( users, (char *)tempelem3->data, strlen( (char *)tempelem3->data ) + 1 );
              tempelem3 = tempelem3->nextnode;
	    }
          tempelem2 = tempelem2->nextnode;
        }
      tempelem1 = tempelem1->nextnode;
    }

  /* pass through any error from __auth_get_every_match() */
  return ( known < 0 ? known : 0 );
}


/* recursive function, should be called with checks == -1 from main routine */
int auth_check_user (const char *address, struct list *userids, int checks) 
{
  int occurences=0, r;
  /*int i; unused variable */
  int j;
  char query[AUTH_QUERY_SIZE];
  char *fields[] = { _ldap_cfg.field_nid, NULL };
  int c1, c2, c3;
  int count1, count2, count3;
  struct list templist;
  struct element *tempelem1, *tempelem2, *tempelem3;

  
  trace(TRACE_DEBUG,"auth_check_user(): checking for user [%s]",address);

  if (checks > MAX_CHECKS_DEPTH)
    {
      trace(TRACE_ERROR, "auth_check_user(): maximum checking depth reached, there probably is a loop in your alias table");
      return -1;
    }

  list_init( &templist );
  
  snprintf ( query, AUTH_QUERY_SIZE, "(|(%s=%s)(%s=%s%s))", _ldap_cfg.field_mail, address, _ldap_cfg.field_mailalt, _ldap_cfg.mailaltprefix, address );

  /* we're just using a little counter variable, since we'll use it in the for loop later */
  j = __auth_get_every_match( query, fields, &templist );

  if ( j < 1 ) 
  {
    if ( checks > 0 )
      {
	/* found the last one, this is the deliver to
	 * but checks needs to be bigger then 0 because
	 * else it could be the first query failure */

	list_nodeadd( userids, address, strlen( address ) + 1 );
	trace (TRACE_DEBUG,"auth_check_user(): adding [%s] to deliver_to address",address);
	list_freelist( &templist.start );
	return 1;
      }
    else
      {
	trace (TRACE_DEBUG,"auth_check_user(): user [%s] not in aliases table",address);
	list_freelist( &templist.start );
	return 0; 
      }
  }
      
  /* do the first entry here */
  tempelem1 = list_getstart( &templist );

  count1 = templist.total_nodes;
  for( c1 = 0; c1 < count1; c1++ )
    {
      tempelem2 = list_getstart( (struct list *)tempelem1->data );
      count2 = ((struct list *)tempelem1->data)->total_nodes;
      for( c2 = 0; c2 < count2; c2++ )
        {
          tempelem3 = list_getstart( (struct list *)tempelem2->data );
          count3= ((struct list *)tempelem2->data)->total_nodes;
          for( c3 = 0; c3 < count3; c3++ )
            {
// here begins the meat
      /* do a recursive search for deliver_to */
      trace (TRACE_DEBUG,"auth_check_user(): checking user [%s] to [%s]",address, (char *)tempelem3->data);
 
      r = auth_check_user ((char *)tempelem3->data, userids, (checks < 0) ? 1 : checks+1);
 
      if (r < 0)
        {
          /* loop detected */
 
          if (checks > 0)
            return -1; /* still in recursive call */
 
          if (userids->start)
            {
              list_freelist(&userids->start);
              userids->total_nodes = 0;
            }
 
          return 0; /* report to calling routine: no results */
        }
 
      occurences += r;
// here ends the meat
              tempelem3 = tempelem3->nextnode;
	    }
          list_freelist( &((struct list *)tempelem2->data)->start );
          tempelem2 = tempelem2->nextnode;
        }
      list_freelist( &((struct list *)tempelem1->data)->start );
      tempelem1 = tempelem1->nextnode;
    }
  list_freelist( &templist.start );

  trace(TRACE_DEBUG,"auth_check_user(): executing query, checks [%d]", checks);
  /* trace(TRACE_INFO,"auth_check_user(): user [%s] has [%d] entries",address,occurences); */

  return occurences;
}

	

/*
 * auth_check_user_ext()
 * 
 * As auth_check_user() but adds the numeric ID of the user found
 * to userids or the forward to the fwds.
 * 
 * returns the number of occurences. 
 */
int auth_check_user_ext(const char *address, struct list *userids, struct list *fwds, int checks) 
{
  /*int i; unused variable*/
  int j;
  int occurences = 0;
  u64_t id;
  char *endptr = NULL;
  char query[AUTH_QUERY_SIZE];
  char *fields[] = { _ldap_cfg.field_nid, _ldap_cfg.field_members, _ldap_cfg.field_fwd, _ldap_cfg.field_fwdsave, _ldap_cfg.field_fwdtarget, NULL };
  int c1, c2, c3;
  int count1, count2, count3;
  struct list templist;
  struct element *tempelem1, *tempelem2, *tempelem3;

  trace(TRACE_DEBUG,"auth_check_user_ext(): checking user [%s] in alias table",address);

  /* This is my private line for sending a DN rather than a search */
  if( checks < -1 )
    {
      snprintf( query, AUTH_QUERY_SIZE, "%s", address );
      j = __auth_get_one_entry( query, fields, &templist );
    }
  else
    {
      snprintf( query, AUTH_QUERY_SIZE, "(|(%s=%s)(%s=%s%s))", _ldap_cfg.field_mail, address, _ldap_cfg.field_mailalt, _ldap_cfg.mailaltprefix, address );
      /* we're just using a little counter variable,
       * since we'll use it in the for loop later */
      j = __auth_get_every_match( query, fields, &templist );
    }
  

  trace(TRACE_DEBUG,"auth_check_user_ext(): searching with query [%s]",query);
  trace(TRACE_DEBUG,"auth_check_user_ext(): executing query, checks [%d]", checks);

  if ( j < 1 ) 
  {
    if (checks>0)
      {
	/* found the last one, this is the deliver to
	 * but checks needs to be bigger then 0 because
	 * else it could be the first query failure */

	id = strtoull(address, &endptr, 10);
	if (*endptr == 0)
          {
            /* numeric deliver-to --> this is a userid */
	    list_nodeadd(userids, &id, sizeof(id));
	  }
	else
          {
	    list_nodeadd(fwds, address, strlen(address)+1);
	    free( endptr );
	  }

	trace (TRACE_DEBUG,"auth_check_user_ext(): adding [%s] to deliver_to address", address);
	list_freelist( &templist.start );
	return 1;
      }
    else
      {
	trace (TRACE_DEBUG,"auth_check_user_ext(): user [%s] not in aliases table", address);
	list_freelist( &templist.start );
	return 0; 
      }
  }
      
  trace (TRACE_DEBUG,"auth_check_user_ext(): into checking loop");

  /* do the first entry here */
  tempelem1 = list_getstart( &templist );

  count1 = templist.total_nodes;
  for( c1 = 0; c1 < count1; c1++ )
    {
      int fwdsave = 1;
      int fwdmaysave = 1;
      tempelem2 = list_getstart( (struct list *)tempelem1->data );
      count2 = ((struct list *)tempelem1->data)->total_nodes;
      for( c2 = 0; c2 < count2; c2++ )
        {
          tempelem3 = list_getstart( (struct list *)tempelem2->data );
          count3= ((struct list *)tempelem2->data)->total_nodes;
          for( c3 = 0; c3 < count3; c3++ )
            {
// here begins the meat
              /* Note that the fields are in *reverse*
	       * order from the definition above! */
              if( 4 == c2 ) {
      /* do a recursive search for deliver_to */
      trace (TRACE_DEBUG,"auth_check_user_ext(): looks like a user id" );
      if( fwdsave )
        {
          trace (TRACE_DEBUG,"auth_check_user_ext(): checking user %s to %s",address, (char *)tempelem3->data);
          occurences += auth_check_user_ext((char *)tempelem3->data, userids, fwds, 1);
        }
      else
        {
          trace (TRACE_DEBUG,"auth_check_user_ext(): not checking user %s to %s due to fwdsave=0",address, (char *)tempelem3->data);
        }
              } else
	      if( 3 == c2 ) {
      /* do a recursive search for deliver_to */
      trace (TRACE_DEBUG,"auth_check_user_ext(): looks like a group member" );
      trace (TRACE_DEBUG,"auth_check_user_ext(): checking user %s to %s",address, (char *)tempelem3->data);
      occurences += auth_check_user_ext((char *)tempelem3->data, userids, fwds, -2);
              } else
              if( 2 == c2 ) {
      /* do a recursive search for deliver_to */
      trace (TRACE_DEBUG,"auth_check_user_ext(): looks like a forwarding dn" );
      trace (TRACE_DEBUG,"auth_check_user_ext(): checking user %s to %s",address, (char *)tempelem3->data);
      occurences += auth_check_user_ext((char *)tempelem3->data, userids, fwds, -2);
      /* if the user does not have a forward, their fwdsave will be false
       * but logically, it is true: "save, then forward to nowhere"
       * so here we make sure that before we don't deliver we check:
       *     - that the fwdsave value is false 
       * AND - that there is a forwarding address */
      if( 0 == fwdmaysave )
        fwdsave = 0;
              } else
              if( 1 == c2 ) {
      /* do a recursive search for deliver_to */
      trace (TRACE_DEBUG,"auth_check_user_ext(): looks like a forwarding state" );
      trace (TRACE_DEBUG,"auth_check_user_ext(): checking user %s to %s",address, (char *)tempelem3->data);
      if( 0 == strcasecmp( (char *)tempelem3->data, "true" ) )
        fwdmaysave = 1;
      else if( 0 == strcasecmp( (char *)tempelem3->data, "false" ) )
        fwdmaysave = 0;
              } else
              if( 0 == c2 ) {
      /* do a recursive search for deliver_to */
      trace (TRACE_DEBUG,"auth_check_user_ext(): looks like a forwarding target" );
      /* rip the prefix off of the result */
      {
	char target[AUTH_QUERY_SIZE];
	/* I am much happier now that this is case insensitive :-)
	 * albeit at the cost of complication and uglification...
	 * perhaps this could be made into a separate function... */
	if( 0 == strncasecmp( (char *)tempelem3->data, _ldap_cfg.fwdtargetprefix, strlen( _ldap_cfg.fwdtargetprefix ) ) )
          {
	    /* Offset the pointer by the length of the prefix to skip */
            sscanf( (char *)tempelem3->data + strlen( _ldap_cfg.fwdtargetprefix ), " %s ", &target[0] );
	  }
        else
          {
            /* The prefix wasn't in there, so just use what we got */
	       snprintf( target, AUTH_QUERY_SIZE, "%s", 
			 (char *)tempelem3->data );
          }
        trace (TRACE_DEBUG,"auth_check_user_ext(): checking user %s to %s",address, target);
        occurences += 1;
	list_nodeadd(fwds, target, strlen(target)+1);
      }
              }
              tempelem3 = tempelem3->nextnode;
	    }
          list_freelist( &((struct list *)tempelem2->data)->start );
          tempelem2 = tempelem2->nextnode;
        }
      list_freelist( &((struct list *)tempelem1->data)->start );
      tempelem1 = tempelem1->nextnode;
    }
  list_freelist( &templist.start );
  
  trace(TRACE_DEBUG,"auth_check_user_ext(): executing query, checks [%d]", checks);
  /* trace(TRACE_INFO,"auth_check_user(): user [%s] has [%d] entries",address,occurences); */

  return occurences;
}

	
/* 
 * auth_adduser()
 *
 * adds a new user to the database 
 * and adds a INBOX.. 
 * \bug This does not seem to work.. It should. This makes
 * this function effectively non-functional! 
 * returns a 1 on succes, -1 on failure 
 */
int auth_adduser (char *username, char *password, char *enctype UNUSED, 
		  char *clientid, char *maxmail, u64_t *user_idnr)
{
  int i, j;
  /*int ret; unused variable */
  int NUM_MODS = 9;
  char *kaboom = "123";
  char *cn_values[]  = { username,	NULL };
  char *sn_values[]  = { username,	NULL };
  char *pw_values[]  = { password,	NULL };
  char *obj_values[] = { "top", "person", _ldap_cfg.objectclass,	NULL };
  char *uid_values[] = { username,	NULL };
  char *cid_values[] = { clientid,	NULL };
  char *nid_values[] = { kaboom,	NULL };
  char *max_values[] = { maxmail,	NULL };
  field_t cn_type = "cn";
  field_t sn_type = "sn";
  field_t mail_type = "mail";
  field_t obj_type = "objectClass";
  unsigned _ldap_dn_len;

  assert(user_idnr != NULL);
  *user_idnr = 0;
  auth_reconnect();

  /* Make the malloc for all of the pieces we're about to to sprintf into it */
  _ldap_dn_len = strlen("cn=,") + strlen(username) + strlen(_ldap_cfg.base_dn);
  _ldap_dn = (char*) my_malloc(_ldap_dn_len + 1);

  snprintf( _ldap_dn, _ldap_dn_len, "cn=%s,%s", username, _ldap_cfg.base_dn );
  trace( TRACE_DEBUG, "Adding user with DN of [%s]", _ldap_dn );

  /* Construct the array of LDAPMod structures representing the attributes 
   * of the new entry. There's a 12 byte leak here, better find it... */

  _ldap_mod = ( LDAPMod ** ) my_malloc( ( NUM_MODS + 1 ) * sizeof( LDAPMod * ) );

  if ( _ldap_mod == NULL )
    {
      trace( TRACE_ERROR, "Cannot allocate memory for mods array" );
      return -1;
    }

  for ( i = 0; i < NUM_MODS; i++ )
    {
      if ( ( _ldap_mod[ i ] = ( LDAPMod * ) my_malloc( sizeof( LDAPMod ) ) ) == NULL )
        {
          trace( TRACE_ERROR, "Cannot allocate memory for mods element %d", i );
	  /* Free everything that did get allocated, which is (i-1) elements */
          for( j = 0; j < (i-1); j++ )
            my_free( _ldap_mod[j] );
          my_free( _ldap_mod );
          ldap_msgfree( _ldap_res );
          return -1;
        }
    }

  i=0;
  trace( TRACE_DEBUG, "Starting to define LDAPMod element %d type %s value %s", i, "objectclass", obj_values[0] );
  _ldap_mod[i]->mod_op = LDAP_MOD_ADD;
  _ldap_mod[i]->mod_type = obj_type;
  _ldap_mod[i]->mod_values = obj_values;

  i++;
  trace( TRACE_DEBUG, "Starting to define LDAPMod element %d type %s value %s", i, "cn", cn_values[0] );
  _ldap_mod[i]->mod_op = LDAP_MOD_ADD;
  _ldap_mod[i]->mod_type = cn_type;
  _ldap_mod[i]->mod_values = cn_values;

  i++;
  trace( TRACE_DEBUG, "Starting to define LDAPMod element %d type %s value %s", i, "sn", cn_values[0] );
  _ldap_mod[i]->mod_op = LDAP_MOD_ADD;
  _ldap_mod[i]->mod_type = sn_type;
  _ldap_mod[i]->mod_values = cn_values;

  if( strlen( _ldap_cfg.field_passwd ) > 0 )
    {
      i++;
      trace( TRACE_DEBUG, "Starting to define LDAPMod element %d type %s value %s", i, _ldap_cfg.field_passwd, pw_values[0] );
      _ldap_mod[i]->mod_op = LDAP_MOD_ADD;
      _ldap_mod[i]->mod_type = _ldap_cfg.field_passwd;
      _ldap_mod[i]->mod_values = cn_values;
    }

  i++;
  trace( TRACE_DEBUG, "Starting to define LDAPMod element %d type %s value %s", i, "mail", sn_values[0] );
  _ldap_mod[i]->mod_op = LDAP_MOD_ADD;
  _ldap_mod[i]->mod_type = mail_type;
  _ldap_mod[i]->mod_values = sn_values;

  i++;
  trace( TRACE_DEBUG, "Starting to define LDAPMod element %d type %s value %s", i, _ldap_cfg.field_uid, uid_values[0] );
  _ldap_mod[i]->mod_op = LDAP_MOD_ADD;
  _ldap_mod[i]->mod_type = _ldap_cfg.field_uid;
  _ldap_mod[i]->mod_values = uid_values;
  i++;
  trace( TRACE_DEBUG, "Starting to define LDAPMod element %d type %s value %s", i, _ldap_cfg.field_cid, cid_values[0] );
  _ldap_mod[i]->mod_op = LDAP_MOD_ADD;
  _ldap_mod[i]->mod_type = _ldap_cfg.field_cid;
  _ldap_mod[i]->mod_values = cid_values;

  i++;
  trace( TRACE_DEBUG, "Starting to define LDAPMod element %d type %s value %s", i, _ldap_cfg.field_maxmail, max_values[0] );
  _ldap_mod[i]->mod_op = LDAP_MOD_ADD;
  _ldap_mod[i]->mod_type = _ldap_cfg.field_maxmail;
  _ldap_mod[i]->mod_values = max_values;

  /* FIXME: need to quackulate a free numeric user id number */
  i++;
  trace( TRACE_DEBUG, "Starting to define LDAPMod element %d type %s value %s", i, _ldap_cfg.field_nid, nid_values[0] );
  _ldap_mod[i]->mod_op = LDAP_MOD_ADD;
  _ldap_mod[i]->mod_type = _ldap_cfg.field_nid;
  _ldap_mod[i]->mod_values = nid_values;

  i++;
  trace( TRACE_DEBUG, "Placing a NULL to terminate the LDAPMod array at element %d", i );
  _ldap_mod[i] = NULL;

  trace( TRACE_DEBUG, "auth_adduser(): calling ldap_add_s( _ldap_conn, _ldap_dn, _ldap_mod )" );
  _ldap_err = ldap_add_s( _ldap_conn, _ldap_dn, _ldap_mod );

  /* make sure to free this stuff even if we do bomb out! */
  /* there's a 12 byte leak here, but I can't figure out how to fix it :-( */
  for( i = 0; i < NUM_MODS; i++ )
    my_free( _ldap_mod[i] );
  my_free( _ldap_mod );

/*  this function should clear the leak, but it segfaults instead :-\ */
/*  ldap_mods_free( _ldap_mod, 1 ); */
  ldap_memfree( _ldap_dn );

  if ( _ldap_err )
    {
      trace(TRACE_ERROR, "auth_adduser(): could not add user: %s", ldap_err2string( _ldap_err ) );
      return -1;
    }

  *user_idnr = strtoull(nid_values[0], 0, 0);
  return 1;
}


int auth_delete_user(const char *username)
{
  auth_reconnect();

  /* look up who's got that username, get their dn, and delete it! */
  if ( !username )
    {
      trace(TRACE_ERROR,"auth_get_userid(): got NULL as useridnr");
      return 0;
    }

  snprintf( _ldap_query, AUTH_QUERY_SIZE, "(%s=%s)", _ldap_cfg.field_uid, username );
  trace(TRACE_DEBUG,"auth_delete_user(): searching with query [%s]", _ldap_query );
  _ldap_err = ldap_search_s( _ldap_conn, _ldap_cfg.base_dn, _ldap_cfg.scope_int, _ldap_query, _ldap_attrs, _ldap_attrsonly, &_ldap_res );
  if ( _ldap_err )
    {
      trace(TRACE_ERROR, "auth_delete_user(): could not execute query: %s", ldap_err2string( _ldap_err ) );
      return -1;
    }

  if ( ldap_count_entries( _ldap_conn, _ldap_res ) < 1 ) 
    {
      trace (TRACE_DEBUG,"auth_delete_user(): no entries found");
      ldap_msgfree( _ldap_res );
      return 0; 
    } 

  _ldap_msg = ldap_first_entry( _ldap_conn, _ldap_res );
  if ( _ldap_msg == NULL )
    {
      ldap_get_option(_ldap_conn, LDAP_OPT_ERROR_NUMBER, &_ldap_err);
      trace(TRACE_ERROR,"auth_delete_user(): ldap_first_entry failed: %s", ldap_err2string( _ldap_err ) );
      ldap_msgfree( _ldap_res );
      return -1;
    }

  _ldap_dn = ldap_get_dn( _ldap_conn, _ldap_msg );

  if ( _ldap_dn )
    {
      trace(TRACE_DEBUG,"auth_delete_user(): deleting user at dn [%s]", _ldap_dn );
      _ldap_err = ldap_delete_s( _ldap_conn, _ldap_dn );
      if( _ldap_err )
        {
          trace(TRACE_ERROR, "auth_delete_user(): could not delete dn: %s", ldap_err2string( _ldap_err ) );
          ldap_memfree( _ldap_dn );
          ldap_msgfree( _ldap_res );
          return -1;
        }
    }

  ldap_memfree( _ldap_dn );
  ldap_msgfree( _ldap_res );
  
  return 0;
}
  
int auth_change_username(u64_t user_idnr, const char *new_name)
{
  int i, j, NUM_MODS = 2;
  char *new_name_str;
  char *new_values[2];
  
  new_name_str = (char *) my_malloc (sizeof(char) * (strlen(new_name) +1));
  strncpy(new_name_str, new_name, strlen(new_name));

  new_values[0] = new_name_str;
  new_values[1] = NULL;

  auth_reconnect();

  if ( !user_idnr )
    {
      trace(TRACE_ERROR,"auth_change_username(): got NULL as useridnr");
      return 0;
    }

  if ( !new_name )
    {
      trace(TRACE_ERROR,"auth_change_username(): got NULL as new_name");
      return 0;
    }

  snprintf( _ldap_query, AUTH_QUERY_SIZE, "(%s=%llu)", _ldap_cfg.field_nid, user_idnr );
  trace(TRACE_DEBUG,"auth_change_username(): searching with query [%s]", _ldap_query );
  _ldap_err = ldap_search_s( _ldap_conn, _ldap_cfg.base_dn, _ldap_cfg.scope_int, _ldap_query, _ldap_attrs, _ldap_attrsonly, &_ldap_res );
  if ( _ldap_err )
    {
      trace(TRACE_ERROR, "auth_change_username(): could not execute query: %s", ldap_err2string( _ldap_err ) );
      return 0;
    }

  if ( ldap_count_entries( _ldap_conn, _ldap_res ) < 1 ) 
    {
      trace (TRACE_DEBUG,"auth_change_username(): no entries found");
      ldap_msgfree( _ldap_res );
      return 0; 
    } 

  _ldap_msg = ldap_first_entry( _ldap_conn, _ldap_res );
  if ( _ldap_msg == NULL )
    {
      ldap_get_option(_ldap_conn, LDAP_OPT_ERROR_NUMBER, &_ldap_err);
      trace(TRACE_ERROR,"auth_change_username(): ldap_first_entry failed: %s", ldap_err2string( _ldap_err ) );
      ldap_msgfree( _ldap_res );
      return 0;
    }

  _ldap_dn = ldap_get_dn( _ldap_conn, _ldap_msg );
  if ( _ldap_dn == NULL )
    {
      ldap_get_option(_ldap_conn, LDAP_OPT_ERROR_NUMBER, &_ldap_err);
      trace(TRACE_ERROR,"auth_change_username(): ldap_get_dn failed: %s", ldap_err2string( _ldap_err ) );
      ldap_msgfree( _ldap_res );
      return -1;
    }
  trace(TRACE_DEBUG,"auth_change_username(): found something at [%s]", _ldap_dn );

  /* Construct the array of LDAPMod structures representing the attributes 
   * of the new entry. */

  _ldap_mod = ( LDAPMod ** ) my_malloc( ( NUM_MODS + 1 ) * sizeof( LDAPMod * ) );

  if ( _ldap_mod == NULL )
    {
      trace( TRACE_ERROR, "Cannot allocate memory for mods array" );
      ldap_memfree( _ldap_dn );
      ldap_msgfree( _ldap_res );
      return -1;
    }

  for ( i = 0; i < NUM_MODS; i++ )
    {
      if ( ( _ldap_mod[ i ] = ( LDAPMod * ) my_malloc( sizeof( LDAPMod ) ) ) == NULL )
        {
          trace( TRACE_ERROR, "Cannot allocate memory for mods element %d", i );
	  /* Free everything that did get allocated, which is (i-1) elements */
          for( j = 0; j < (i-1); j++ )
            my_free( _ldap_mod[j] );
          my_free( _ldap_mod );
          ldap_memfree( _ldap_dn );
          ldap_msgfree( _ldap_res );
          return -1;
        }
    }

  i=0;
  trace( TRACE_DEBUG, "Starting to define LDAPMod element %d type %s value %s", i, _ldap_cfg.field_uid, new_values[0] );
  _ldap_mod[i]->mod_op = LDAP_MOD_REPLACE;
  _ldap_mod[i]->mod_type = _ldap_cfg.field_uid;
  _ldap_mod[i]->mod_values = new_values;

  i++;
  trace( TRACE_DEBUG, "Placing a NULL to terminate the LDAPMod array at element %d", i );
  _ldap_mod[i] = NULL;

  trace( TRACE_DEBUG, "auth_change_username(): calling ldap_modify_s( _ldap_conn, _ldap_dn, _ldap_mod )" );
  _ldap_err = ldap_modify_s( _ldap_conn, _ldap_dn, _ldap_mod );

  /* make sure to free this stuff even if we do bomb out! */
  for( i = 0; i < NUM_MODS; i++ )
    my_free( _ldap_mod[i] );
  my_free( _ldap_mod );

  ldap_memfree( _ldap_dn );
  ldap_msgfree( _ldap_res );
  
  if ( _ldap_err )
    {
      trace(TRACE_ERROR, "auth_change_username(): could not change username: %s", ldap_err2string( _ldap_err ) );
      return -1;
    }

  return 1;
}


int auth_change_password(u64_t user_idnr UNUSED, const char *new_pass UNUSED, const char *enctype UNUSED)
{

  return -1;
}


int auth_change_clientid(u64_t user_idnr, u64_t newcid)
{
  int i, j, NUM_MODS = 2;
  char newcid_str[100];
  char *new_values[] = { newcid_str,	NULL };

  auth_reconnect();

  if ( !user_idnr )
    {
      trace(TRACE_ERROR,"auth_change_clientid(): got NULL as useridnr");
      return 0;
    }

  if ( !newcid )
    {
      trace(TRACE_ERROR,"auth_change_clientid(): got NULL as newcid");
      return 0;
    }

  snprintf( new_values[0], 100, "%llu", newcid ); // Yeah, something like this...

  snprintf( _ldap_query, AUTH_QUERY_SIZE, "(%s=%llu)", _ldap_cfg.field_nid, user_idnr );
  trace(TRACE_DEBUG,"auth_change_clientid(): searching with query [%s]", _ldap_query );
  _ldap_err = ldap_search_s( _ldap_conn, _ldap_cfg.base_dn, _ldap_cfg.scope_int, _ldap_query, _ldap_attrs, _ldap_attrsonly, &_ldap_res );
  if ( _ldap_err )
    {
      trace(TRACE_ERROR, "auth_change_clientid(): could not execute query: %s", ldap_err2string( _ldap_err ) );
      return 0;
    }

  if ( ldap_count_entries( _ldap_conn, _ldap_res ) < 1 ) 
    {
      trace (TRACE_DEBUG,"auth_change_clientid(): no entries found");
      ldap_msgfree( _ldap_res );
      return 0; 
    } 

  _ldap_msg = ldap_first_entry( _ldap_conn, _ldap_res );
  if ( _ldap_msg == NULL )
    {
      ldap_get_option(_ldap_conn, LDAP_OPT_ERROR_NUMBER, &_ldap_err);
      trace(TRACE_ERROR,"auth_change_clientid(): ldap_first_entry failed: %s", ldap_err2string( _ldap_err ) );
      ldap_msgfree( _ldap_res );
      return 0;
    }

  _ldap_dn = ldap_get_dn( _ldap_conn, _ldap_msg );
  if ( _ldap_dn == NULL )
    {
      ldap_get_option(_ldap_conn, LDAP_OPT_ERROR_NUMBER, &_ldap_err);
      trace(TRACE_ERROR,"auth_change_clientid(): ldap_get_dn failed: %s", ldap_err2string( _ldap_err ) );
      ldap_msgfree( _ldap_res );
      return -1;
    }
  trace(TRACE_DEBUG,"auth_change_clientid(): found something at [%s]", _ldap_dn );

  /* Construct the array of LDAPMod structures representing the attributes 
   * of the new entry. */

  _ldap_mod = ( LDAPMod ** ) my_malloc( ( NUM_MODS + 1 ) * sizeof( LDAPMod * ) );

  if ( _ldap_mod == NULL )
    {
      trace( TRACE_ERROR, "Cannot allocate memory for mods array" );
      ldap_memfree( _ldap_dn );
      ldap_msgfree( _ldap_res );
      return -1;
    }

  for ( i = 0; i < NUM_MODS; i++ )
    {
      if ( ( _ldap_mod[ i ] = ( LDAPMod * ) my_malloc( sizeof( LDAPMod ) ) ) == NULL )
        {
          trace( TRACE_ERROR, "Cannot allocate memory for mods element %d", i );
	  /* Free everything that did get allocated, which is (i-1) elements */
          for( j = 0; j < (i-1); j++ )
            my_free( _ldap_mod[j] );
          my_free( _ldap_mod );
          ldap_memfree( _ldap_dn );
          ldap_msgfree( _ldap_res );
          return -1;
        }
    }

  i=0;
  trace( TRACE_DEBUG, "Starting to define LDAPMod element %d type %s value %s", i, _ldap_cfg.field_cid, new_values[0] );
  _ldap_mod[i]->mod_op = LDAP_MOD_REPLACE;
  _ldap_mod[i]->mod_type = _ldap_cfg.field_cid;
  _ldap_mod[i]->mod_values = new_values;

  i++;
  trace( TRACE_DEBUG, "Placing a NULL to terminate the LDAPMod array at element %d", i );
  _ldap_mod[i] = NULL;

  trace( TRACE_DEBUG, "auth_change_clientid(): calling ldap_modify_s( _ldap_conn, _ldap_dn, _ldap_mod )" );
  _ldap_err = ldap_modify_s( _ldap_conn, _ldap_dn, _ldap_mod );

  /* make sure to free this stuff even if we do bomb out! */
  for( i = 0; i < NUM_MODS; i++ )
    my_free( _ldap_mod[i] );
  my_free( _ldap_mod );

  ldap_memfree( _ldap_dn );
  ldap_msgfree( _ldap_res );
  
  if ( _ldap_err )
    {
      trace(TRACE_ERROR, "auth_change_clientid(): could not change clientid: %s", ldap_err2string( _ldap_err ) );
      return -1;
    }

  return 1;
}

int auth_change_mailboxsize(u64_t user_idnr, u64_t new_size)
{
  int i, j, NUM_MODS = 2;
  char newsize_str[100];
  char *new_values[] = { newsize_str,	NULL };

  auth_reconnect();

  if ( !user_idnr )
    {
      trace(TRACE_ERROR,"auth_change_mailboxsize(): got NULL as useridnr");
      return 0;
    }

  if ( !new_size )
    {
      trace(TRACE_ERROR,"auth_change_mailboxsize(): got NULL as newsize");
      return 0;
    }

  snprintf( new_values[0], 100, "%llu", new_size );

  snprintf( _ldap_query, AUTH_QUERY_SIZE, "(%s=%llu)", _ldap_cfg.field_nid, user_idnr );
  trace(TRACE_DEBUG,"auth_change_mailboxsize(): searching with query [%s]", _ldap_query );
  _ldap_err = ldap_search_s( _ldap_conn, _ldap_cfg.base_dn, _ldap_cfg.scope_int, _ldap_query, _ldap_attrs, _ldap_attrsonly, &_ldap_res );
  if ( _ldap_err )
    {
      trace(TRACE_ERROR, "auth_change_mailboxsize(): could not execute query: %s", ldap_err2string( _ldap_err ) );
      return 0;
    }

  if ( ldap_count_entries( _ldap_conn, _ldap_res ) < 1 ) 
    {
      trace (TRACE_DEBUG,"auth_change_mailboxsize(): no entries found");
      ldap_msgfree( _ldap_res );
      return 0; 
    } 

  _ldap_msg = ldap_first_entry( _ldap_conn, _ldap_res );
  if ( _ldap_msg == NULL )
    {
      ldap_get_option(_ldap_conn, LDAP_OPT_ERROR_NUMBER, &_ldap_err);
      trace(TRACE_ERROR,"auth_change_mailboxsize(): ldap_first_entry failed: %s", ldap_err2string( _ldap_err ) );
      ldap_msgfree( _ldap_res );
      return 0;
    }

  _ldap_dn = ldap_get_dn( _ldap_conn, _ldap_msg );
  if ( _ldap_dn == NULL )
    {
      ldap_get_option(_ldap_conn, LDAP_OPT_ERROR_NUMBER, &_ldap_err);
      trace(TRACE_ERROR,"auth_change_mailboxsize(): ldap_get_dn failed: %s", ldap_err2string( _ldap_err ) );
      ldap_msgfree( _ldap_res );
      return -1;
    }
  trace(TRACE_DEBUG,"auth_change_mailboxsize(): found something at [%s]", _ldap_dn );

  /* Construct the array of LDAPMod structures representing the attributes 
   * of the new entry. */

  _ldap_mod = ( LDAPMod ** ) my_malloc( ( NUM_MODS + 1 ) * sizeof( LDAPMod * ) );

  if ( _ldap_mod == NULL )
    {
      trace( TRACE_ERROR, "Cannot allocate memory for mods array" );
      ldap_memfree( _ldap_dn );
      ldap_msgfree( _ldap_res );
      return -1;
    }

  for ( i = 0; i < NUM_MODS; i++ )
    {
      if ( ( _ldap_mod[ i ] = ( LDAPMod * ) my_malloc( sizeof( LDAPMod ) ) ) == NULL )
        {
          trace( TRACE_ERROR, "Cannot allocate memory for mods element %d", i );
	  /* Free everything that did get allocated, which is (i-1) elements */
          for( j = 0; j < (i-1); j++ )
            my_free( _ldap_mod[j] );
          my_free( _ldap_mod );
          ldap_memfree( _ldap_dn );
          ldap_msgfree( _ldap_res );
          return -1;
        }
    }

  i=0;
  trace( TRACE_DEBUG, "Starting to define LDAPMod element %d type %s value %s", i, _ldap_cfg.field_maxmail, new_values[0] );
  _ldap_mod[i]->mod_op = LDAP_MOD_REPLACE;
  _ldap_mod[i]->mod_type = _ldap_cfg.field_maxmail;
  _ldap_mod[i]->mod_values = new_values;

  i++;
  trace( TRACE_DEBUG, "Placing a NULL to terminate the LDAPMod array at element %d", i );
  _ldap_mod[i] = NULL;

  trace( TRACE_DEBUG, "auth_change_mailboxsize(): calling ldap_modify_s( _ldap_conn, _ldap_dn, _ldap_mod )" );
  _ldap_err = ldap_modify_s( _ldap_conn, _ldap_dn, _ldap_mod );

  /* make sure to free this stuff even if we do bomb out! */
  for( i = 0; i < NUM_MODS; i++ )
    my_free( _ldap_mod[i] );
  my_free( _ldap_mod );

  ldap_memfree( _ldap_dn );
  ldap_msgfree( _ldap_res );
  
  if ( _ldap_err )
    {
      trace(TRACE_ERROR, "auth_change_mailboxsize(): could not change mailboxsize: %s", ldap_err2string( _ldap_err ) );
      return -1;
    }

  return 1;
}


/* 
 * auth_validate()
 *
 * tries to validate user 'user'
 *
 * returns useridnr on OK, 0 on validation failed, -1 on error 
 */
int auth_validate (char *username, char *password, u64_t *user_idnr)
{
	timestring_t timestring;
     
	int ldap_err;
	char *ldap_dn = NULL;
	char *id_char = NULL;
	char query[AUTH_QUERY_SIZE];
	/*char *fields[] = { "dn", _ldap_cfg.field_nid, NULL }; unused variable */
     
	assert(user_idnr != NULL);
	*user_idnr = 0;
	create_current_timestring(&timestring);
  snprintf( query, AUTH_QUERY_SIZE, "(%s=%s)", _ldap_cfg.field_uid, username );
//  ldap_dn = __auth_get_first_match( query, fields ); 
//  id_char = __auth_get_first_match( query, _ldap_cfg.field_nid ); 

  /* now, try to rebind as the given DN using the supplied password */
  trace (TRACE_ERROR,"auth_validate(): rebinding as [%s] to validate password", ldap_dn);
  
  ldap_err = ldap_bind_s( _ldap_conn, ldap_dn, password, LDAP_AUTH_SIMPLE );
  
  // FIXME: do we need to bind back to the dbmail "superuser" again?
  // FIXME: not at the moment because the db_reconnect() will do it for us
  if( ldap_err )
  {
      trace(TRACE_ERROR,"auth_validate(): ldap_bind_s failed: %s", ldap_err2string( ldap_err ) );
      user_idnr = 0;
  }
  else 
  {
       *user_idnr = (id_char) ? strtoull(id_char, NULL, 10) : 0;
       trace (TRACE_ERROR,"auth_validate(): return value is [%llu]", 
	      *user_idnr);
       
      /* FIXME: implement this in LDAP...  log login in the database
	 snprintf(__auth_query_data, AUTH_QUERY_SIZE, "UPDATE users SET last_login = '%s' "
	 "WHERE user_idnr = '%llu'", timestring, id);
	 
	 if (__auth_query(__auth_query_data)==-1)
	 trace(TRACE_ERROR, "auth_validate(): could not update user login time");
      */
  }
  
  if( id_char ) free( id_char );
  if( ldap_dn ) ldap_memfree( ldap_dn );
  
  if (*user_idnr == 0) 
       return 0;
  else
       return 1;
}

/* returns useridnr on OK, 0 on validation failed, -1 on error */
u64_t auth_md5_validate (char *username UNUSED,unsigned char *md5_apop_he UNUSED, char *apop_stamp UNUSED)
{
  
  return 0;
}

/*
//  {
//  int c1, c2, c3;
//  int count1, count2, count3;
//  struct list templist;
//  struct element *tempelem1, *tempelem2, *tempelem3;
//
//  // do the first entry here 
//  tempelem1 = list_getstart( retlist );
//  count1 = retlist->total_nodes;
//
//  // we'll get the next entry at the _end_ of the loop! 
//  printf( "retlist has %d nodes\n", retlist->total_nodes );
//  for( c1 = 0; c1 < count1; c1++ )
//    {
//      tempelem2 = list_getstart( (struct list *)tempelem1->data );
//      count2 = ((struct list *)tempelem1->data)->total_nodes;
//      for( c2 = 0; c2 < count2; c2++ )
//        {
//          //if( tempelem2 )
//          tempelem3 = list_getstart( (struct list *)tempelem2->data );
//          count3 = ((struct list *)tempelem2->data)->total_nodes;
//          for( c3 = 0; c3 < count3; c3++ )
//            {
//              printf( "I've got %s\n", tempelem3->data );
//              tempelem3 = tempelem3->nextnode;
//              //if( tempelem3->nextnode ) tempelem3 = tempelem3->nextnode;
//	      //  else { printf(" break at %d\n", __LINE__ ); break; }
//	    }
//          //if( tempelem2->nextnode ) tempelem2 = tempelem2->nextnode;
//          tempelem2 = tempelem2->nextnode;
//           // else { printf(" break at %d\n", __LINE__ ); break; }
//        }
//      tempelem1 = tempelem1->nextnode;
//      //if( tempelem1->nextnode ) tempelem1 = tempelem1->nextnode;
//      //  else { printf(" break at %d\n", __LINE__ ); break; }
//    }
//  }
//*/

