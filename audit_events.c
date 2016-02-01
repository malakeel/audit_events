#include <postgres.h>
 
#include <access/sysattr.h>
 
#include <catalog/pg_class.h>
#include <catalog/pg_type.h>
 
#include <nodes/parsenodes.h>
//#include "replication/output_plugin.h"
#include <replication/logical.h>
 
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/rel.h"
#include "utils/relcache.h"
#include "utils/syscache.h"
#include "utils/typcache.h"
 
#include <miscadmin.h>
#include <commands/dbcommands.h>
 
#include <audit_events.h>
#include <serializer.h>


PG_MODULE_MAGIC;

extern void _PG_init(void);
extern void _PG_output_plugin_init(OutputPluginCallbacks *cb);


static void audit_plugin_startup(LogicalDecodingContext *ctx,
								 OutputPluginOptions *opt,
								 bool is_init);

static void on_begin_txn(LogicalDecodingContext *ctx,
						 ReorderBufferTXN *txn);

static void audit_plugin_shutdown(LogicalDecodingContext *ctx);

static void on_commit_txn(LogicalDecodingContext *ctx,
						  ReorderBufferTXN *txn,
						  XLogRecPtr commit_lsn);

static void on_row_change(LogicalDecodingContext *ctx,
						  ReorderBufferTXN *txn,
						  Relation rel,
						  ReorderBufferChange *change);

static void populate_fields(StringInfo s, TupleDesc tupdesc,
							HeapTuple oldtuple , HeapTuple newtuple ,
							EventData * event_data);

static bool are_equal( HeapTuple tuple1,
					   HeapTuple tuple2,
					   TupleDesc tupleDesc ) ;

void _PG_init(void)
{
	/* other plugins can perform things here */
}

/* specify output plugin callbacks */
void _PG_output_plugin_init(OutputPluginCallbacks *cb)
{
	AssertVariableIsOfType(&_PG_output_plugin_init, LogicalOutputPluginInit);

	cb->startup_cb = audit_plugin_startup;
	cb->begin_cb = on_begin_txn;
	cb->change_cb = on_row_change;
	cb->commit_cb = on_commit_txn;
	cb->shutdown_cb = audit_plugin_shutdown;
  
}

/* initialize this plugin */
static void audit_plugin_startup(LogicalDecodingContext *ctx,
								 OutputPluginOptions *opt, bool is_init)
{

	EventData *data;
  
	data = palloc0(sizeof(EventData));

	data->context = AllocSetContextCreate(ctx->context,
										  "text conversion context",
										  ALLOCSET_DEFAULT_MINSIZE,
										  ALLOCSET_DEFAULT_INITSIZE,
										  ALLOCSET_DEFAULT_MAXSIZE);
  
	ctx->output_plugin_private = data;

	opt->output_type = OUTPUT_PLUGIN_TEXTUAL_OUTPUT;
	
}


/* BEGIN callback */
static void on_begin_txn(LogicalDecodingContext *ctx, ReorderBufferTXN *txn)
{

	bool last_write = true ;
	
	OutputPluginPrepareWrite(ctx, last_write);

	appendStringInfo(ctx->out, "BEGIN %u", txn->xid);

	OutputPluginWrite(ctx, last_write);
}


/* COMMIT callback */
static void on_commit_txn(LogicalDecodingContext *ctx, ReorderBufferTXN *txn, XLogRecPtr commit_lsn)
{
	OutputPluginPrepareWrite(ctx, true);
	appendStringInfo(ctx->out, "COMMIT Transaction %u", txn->xid);
	OutputPluginWrite(ctx, true);
}

/* cleanup this plugin's resources */
static void audit_plugin_shutdown(LogicalDecodingContext *ctx)
{
	EventData *data = ctx->output_plugin_private;
	/* cleanup our own resources via memory context reset */
	MemoryContextDelete(data->context);
}


/*
 * callback for individual changed tuples
 */
static void on_row_change(LogicalDecodingContext *ctx, ReorderBufferTXN *txn, Relation relation, ReorderBufferChange *change)
{

	EventData *data;
	Form_pg_class class_form;
	TupleDesc	tupdesc;
	MemoryContext old;
	//char * schema ;
	HeapTuple oldtuple;
	HeapTuple newtuple;

	data = ctx->output_plugin_private;

	class_form = RelationGetForm(relation);

	tupdesc = RelationGetDescr(relation);

	/* Avoid leaking memory by using and resetting our own context */
	old = MemoryContextSwitchTo(data->context);
	
	data->tx_id = txn->xid ;
	
	data->database = get_database_name(MyDatabaseId);

	data->schema = get_namespace_name(get_rel_namespace(RelationGetRelid(relation)));
		
	data->table =  NameStr(class_form->relname) ;

	//data->rel_id = class_form->rel ;

	sprintf(data->time_stamp , "%s",  timestamptz_to_str(txn->commit_time));

	oldtuple =  &change->data.tp.oldtuple->tuple ;
	newtuple =  &change->data.tp.newtuple->tuple ;
	
	switch (change->action)
    {
    case REORDER_BUFFER_CHANGE_INSERT:
		strcpy(data->action,"INSERT");
		populate_fields(ctx->out, tupdesc, NULL , newtuple, data);
		break;
		
    case REORDER_BUFFER_CHANGE_UPDATE:
		strcpy(data->action,"UPDATE");
		populate_fields(ctx->out, tupdesc, oldtuple , newtuple, data);
		break;
			
    case REORDER_BUFFER_CHANGE_DELETE:
		strcpy(data->action,"DELETE");
		populate_fields(ctx->out, tupdesc, oldtuple , NULL , data);
		break;
			
    default:
		Assert(false);
    }


	OutputPluginPrepareWrite(ctx, true);	
	serialize( ctx->out , data ) ;
	
	free(data->fields);
	
	MemoryContextSwitchTo(old);
	MemoryContextReset(data->context);

	OutputPluginWrite(ctx, true);
}


static void populate_fields(StringInfo s, TupleDesc tupdesc, HeapTuple oldtuple , HeapTuple newtuple , EventData * event_data)
{
	int	num_att;
	int idx = 0; 
	Field * field  ;

	bool dirty = true ;
  
	num_att = tupdesc->natts ;

	event_data->_fields_count = num_att ;
	
	event_data->fields = malloc (sizeof(Field) * tupdesc->natts);
	
	field = event_data->fields ;

  
	/* print all columns individually */
	for (idx = 0; idx < num_att; idx++)
    {
		Form_pg_attribute attr; /* the attribute itself */
		//Oid			type_id;		/* type of current attribute */
		Oid			typoutput;	/* output function */
		bool		typisvarlena;
		bool		is_old_null;		/* column is null? */
		bool		is_new_null;		/* column is null? */

		Datum		old_val;	/* possibly toasted Datum */
		Datum		new_val;	/* possibly toasted Datum */
		attr = tupdesc->attrs[idx];

		/*
		 * Ignore dropped columns, we can't be sure everything is available for them
		 */
		if (attr->attisdropped)
			continue;
      
		/*
		 * Ignore system columns, oid will already have been printed if present.
		 */
		if (attr->attnum < 0)
			continue;

		field->ordinal = idx + 1 ;
		
		field->type_id = attr->atttypid ;
		
		sprintf(field->name ,"%s", quote_identifier(NameStr(attr->attname))) ;
		
		//sprintf(field->type ,"%s", format_type_be(type_id));
		
		/* query output function */
		getTypeOutputInfo(field->type_id,  &typoutput, &typisvarlena);

		field->output_type = typoutput ;
		if(oldtuple != NULL )
		{
			/* get Datum from tuple */
			old_val = heap_getattr(oldtuple, idx + 1, tupdesc, &is_old_null);
			//sprintf(field->old_val  ,"%s", OidOutputFunctionCall(typoutput, old_val));
			field->old_val = old_val;
		}else
			field->old_val = (Datum) NULL;
		if (newtuple != NULL){
			new_val = heap_getattr(newtuple, idx + 1, tupdesc, &is_new_null);
			//sprintf(field->new_val  ,"%s", OidOutputFunctionCall(typoutput, new_val));
			field->new_val = new_val;
		}else
			field->new_val = (Datum) NULL;

		//if(oldtuple != NULL && newtuple != NULL)


		event_data->is_modified = dirty ;

		field++;

    }
	event_data->is_modified = !are_equal(oldtuple , newtuple , tupdesc) ;
}


bool are_equal(	HeapTuple tuple1 , HeapTuple tuple2 , TupleDesc tupleDesc ) 
{

	Datum res ;
  
	bool result ;
  
	FmgrInfo *flinfo ;
  
	FunctionCallInfoData fcinfo;  /* or allocate this somewhere */

	if (tuple1 == NULL || tuple2 == NULL)
		return false;

	flinfo = palloc(sizeof(FmgrInfo));
 
	fmgr_info(fmgr_internal_function("record_image_eq"), flinfo);
 
	/* need this for each new fcinfo, but one fcinfo can be reused for multiple calls */
	InitFunctionCallInfoData(fcinfo, flinfo,
							 2, /* no. of args */
							 InvalidOid,  /* collation */
							 NULL, /* context - NULL if not called from sql */
							 NULL /* resultinfo - NULL if not expected to return sets */);

	/* following is needed per call: */
	fcinfo.arg[0] =  heap_copy_tuple_as_datum(tuple1 , tupleDesc);
	fcinfo.arg[1] =  heap_copy_tuple_as_datum(tuple2 , tupleDesc);
	fcinfo.argnull[0] = false;
	fcinfo.argnull[1] = false;
 
	res  =  FunctionCallInvoke(&fcinfo);
	result  = DatumGetBool(res) ;
  
	pfree(flinfo) ;

	//  record_image_eq();
	return result ;
    
}

 
/* bool _are_equal_(Datum d1 , Datum d2 ){ */
  
/*   Datum res ; */
/*   bool result = false ; */
/*   FmgrInfo *flinfo = palloc(sizeof(FmgrInfo)); */
/*   FunctionCallInfoData fcinfo;  /\* or allocate this somewhere *\/ */
		  
/*   fmgr_info(fmgr_internal_function("record_image_eq"), flinfo); */
		     
/*   /\* need this for each new fcinfo, but one fcinfo can be reused for multiple calls *\/ */
/*   InitFunctionCallInfoData(fcinfo, flinfo, */
/* 			   2, /\* no. of args *\/ */
/* 			   InvalidOid,  /\* collation *\/ */
/* 			   NULL, /\* context - NULL if not called from sql *\/ */
/* 			   NULL /\* resultinfo - NULL if not expected to return sets *\/); */
																      
/*   /\* following is needed per call: *\/ */
/*   fcinfo.arg[0] = d1; */
/*   fcinfo.arg[1] = d2; */
/*   fcinfo.argnull[0] = false; */
/*   fcinfo.argnull[1] = false; */
  
/*   res  = (Datum) FunctionCallInvoke(&fcinfo); */
  
/*   result  = DatumGetBool(res) ; */
  
/*   pfree(flinfo) ; */
/*   return result ; */
/* } */
