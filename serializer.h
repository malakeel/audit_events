#ifndef PG_AUDIT_EVENT_SERIALIZE
#define PG_AUDIT_EVENT_SERIALIZE

#include "audit_events.h"

extern void serialize(StringInfo s , EventData * event) ;

#endif

