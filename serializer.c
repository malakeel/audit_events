#include <postgres.h>

#include <access/sysattr.h>

#include <catalog/pg_class.h>
#include <catalog/pg_type.h>

#include <nodes/parsenodes.h>
#include "replication/output_plugin.h"
#include "replication/logical.h"

#include <serializer.h>


static void print_literal(StringInfo s, Oid typid,  char *outputstr);


/**
 *  Prints the key and value, quotes the strings (without escaping), and 
 *  appends a comma, and new line
 */

static void output_string_key_value(StringInfo s , char * k , char * v) {
	appendStringInfo(s, "%s: \"%s\",\n" , k , v);
}

static void serialize_json(StringInfo s, EventData * event){

	int fields_count = event->_fields_count;
	int i = 0 ;
	Field f ;
	
	appendStringInfoString(s , "\n__START_PG_TUPLE_EVENT__\n");		
	appendStringInfoString(s , "{\n");
	appendStringInfo(s , "tx_id: %u," , event->tx_id);
	appendStringInfoString(s , "\n" );

	output_string_key_value(s , "application_name" , event->app_name) ;
	/* appendStringInfoString(s , "\n" ); */

	output_string_key_value(s , "username" , event->session_user) ;
	/* appendStringInfoString(s , "\n" ); */


	output_string_key_value(s , "database" , event->database) ;
	/* appendStringInfoString(s , "\n" ); */
	
	output_string_key_value(s , "schema" , event->schema) ;
	/* appendStringInfoString(s , "\n" ); */

	output_string_key_value(s , "table" , event->table) ;
	output_string_key_value(s , "action" , event->action) ;
	output_string_key_value(s , "time-stamp" , event->time_stamp) ;

	//output_string_key_value(s , "modified" , event->is_modified) ;
	appendStringInfo(s , "modified: %s," , event->is_modified ? "true" : "false" ) ;
	appendStringInfoString(s , "\n" );

	appendStringInfo(s , "fields: [") ;
	appendStringInfo(s , "\n");
	
	for (i=0 ; i < fields_count ; i++){
		f = event->fields[i];
		appendStringInfo(s , "{");
		appendStringInfo(s , "ordinal: %u , " , f.ordinal);
		appendStringInfo(s , "name: %s , " , f.name );
		appendStringInfo(s , "type-id: %u , " , f.type_id );
		//appendStringInfo(s , "type: %s , " , f.type );

		if(f.old_val)
		{
			appendStringInfoString(s , "previous-value: " );
			print_literal( s , f.output_type ,  OidOutputFunctionCall( f.output_type , f.old_val) ) ;
			if(f.new_val)
				appendStringInfoString(s , "," );
			}
		if(f.new_val)
		{
			appendStringInfoString(s , "current-value: ");
			print_literal( s , f.output_type ,  OidOutputFunctionCall( f.output_type , f.new_val) ) ;
		}
		appendStringInfo(s , "}");
		
		if(i < ( fields_count -1))
			appendStringInfo(s , ",\n");
		else
			appendStringInfo(s , "\n");
		
	}

	appendStringInfoString(s , "]" );
	appendStringInfoString(s , "}\n" );
	appendStringInfo(s , "number_of_fields: %u" , i );
	appendStringInfo(s , "\n__END_PG_TUPLE_EVENT__\n");

}

/* static void output_field(StringInfo s, char * field ,Oid typid, char * fmt , char *outputstr){ */
/* 	char * val ; */
/* 	appendStringInfo(s , fmt , val ) ; */
/* } */

/**
 * Print literal `outputstr' already represented as string of type `typid'
 * into stringbuf `s'.
 *
 * Some builtin types aren't quoted, the rest is quoted. 
 * Escaping is done as if standard_conforming_strings were enabled in postgresql.conf
 */
static void print_literal(StringInfo s, Oid typid,  char *outputstr)
{
	const char *valptr;
	
	switch (typid)
	{
		case INT2OID:
		case INT4OID:
		case INT8OID:
		case OIDOID:
		case FLOAT4OID:
		case FLOAT8OID:
		case NUMERICOID:
			/* NB: We don't care about Inf, NaN et al. */
			appendStringInfoString(s, outputstr);
			break;

		case BITOID:
		case VARBITOID:
			appendStringInfo(s, "B'%s'", outputstr);
			break;

		case BOOLOID:
			if (strcmp(outputstr, "t") == 0)
				appendStringInfoString(s, "true");
			else
				appendStringInfoString(s, "false");
			break;

		default:
			appendStringInfoChar(s, '\'');
			for (valptr = outputstr; *valptr; valptr++)
			{
				char		ch = *valptr;

				if (SQL_STR_DOUBLE(ch, false))
					appendStringInfoChar(s, ch);
				appendStringInfoChar(s, ch);
			}
			appendStringInfoChar(s, '\'');
			break;
	}
}



void serialize(StringInfo s , EventData * event){

	serialize_json(s  , event ) ;
}
