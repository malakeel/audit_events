#ifndef PG_AUDIT_EVENT
#define PG_AUDIT_EVENT

#include <postgres.h>


typedef struct {
	short ordinal ;
	short type_id ;
	Oid output_type;
	//char type [128] ;
	char name [128] ;
	Datum old_val ;
	Datum new_val ;
} Field ;



/***
 *  Event represets a modification in the state of the DB. 
 *  We like to capture as much information as possible, to match audit trigger
 *  https://wiki.postgresql.org/wiki/Audit_trigger_91plus
 **/


typedef struct {
	
	MemoryContext context;
	
	unsigned  tx_id ;
	char  action[8] ; 
	char  time_stamp[64] ;
	char*  session_user;
	char*  app_name ;
	char*  server_id ;

	/* The name of the database generated the event	 */
	char*  database ;


	/* The name of the schema generated the event	 */
	char*  schema ;


	/* The name of the table generated the event	 */
	char*  table ;

	
	int   rel_id ; // or Oid (Object Id or relation Id that generated the event)
	
	Field *fields ;
	short _fields_count ;

	/***
	 * In case of UPDATE, did the values really change ? 
	 */
	bool is_modified ;

	
} EventData;





#endif
