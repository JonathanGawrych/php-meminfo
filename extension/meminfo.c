#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_meminfo.h"

#include "ext/standard/basic_functions.h"
#include "ext/standard/info.h"
#include "ext/standard/php_string.h"

#include "zend_extensions.h"
#include "zend_exceptions.h"
#include "Zend/zend_closures.h"
#include "Zend/zend_compile.h"

#include "zend.h"
#include "SAPI.h"
#include "zend_API.h"


#if PHP_VERSION_ID >= 80000
ZEND_BEGIN_ARG_INFO_EX(arginfo_meminfo_dump, 0, 0, 1)
    ZEND_ARG_INFO(0, output_stream)
ZEND_END_ARG_INFO()

const zend_function_entry meminfo_functions[] = {
    PHP_FE(meminfo_dump, arginfo_meminfo_dump)
    PHP_FE_END
};
#else
const zend_function_entry meminfo_functions[] = {
    PHP_FE(meminfo_dump, NULL)
    PHP_FE_END
};
#endif

zend_module_entry meminfo_module_entry = {
    STANDARD_MODULE_HEADER,
    "meminfo",
    meminfo_functions,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    MEMINFO_VERSION,
    STANDARD_MODULE_PROPERTIES
};


/**
 * Generate a JSON output of the list of items in memory (objects, arrays, string, etc...)
 * with their sizes and other information
 */
PHP_FUNCTION(meminfo_dump)
{
    zval *zval_stream;

    php_stream *stream;
    HashTable visited_items;

    if (zend_parse_parameters(ZEND_NUM_ARGS(), "r", &zval_stream) == FAILURE) {
        return;
    }

    zend_hash_init(&visited_items, 1000, NULL, NULL, 0);

    php_stream_from_zval(stream, zval_stream);
    php_stream_printf(stream, "{\n");

    php_stream_printf(stream, "  \"header\" : {\n");
    php_stream_printf(stream, "    \"memory_usage\" : %zd,\n", zend_memory_usage(0));
    php_stream_printf(stream, "    \"memory_usage_real\" : %zd,\n", zend_memory_usage(1));
    php_stream_printf(stream, "    \"peak_memory_usage\" : %zd,\n", zend_memory_peak_usage(0));
    php_stream_printf(stream, "    \"peak_memory_usage_real\" : %zd\n", zend_memory_peak_usage(1));
    php_stream_printf(stream, "  },\n");

    php_stream_printf(stream, "  \"items\": {\n");

    meminfo_stream_info stream_info;
    stream_info.stream = stream;
    stream_info.visited_items = &visited_items;
    stream_info.frame_label[0] = '\0';

    meminfo_browse_exec_frames(&stream_info);
    meminfo_browse_class_static_members(&stream_info);
    meminfo_browse_function_static_variables(&stream_info, "<GLOBAL_FUNCTION>", CG(function_table));
    meminfo_browse_handlers(&stream_info);
    meminfo_browse_object_store(&stream_info);

    php_stream_printf(stream, "\n    }\n");
    php_stream_printf(stream, "}\n}\n");

    zend_hash_destroy(&visited_items);
}

/**
 * Go through all exec frames to gather declared variables and follow them to record items in memory
 */
void meminfo_browse_exec_frames(meminfo_stream_info *stream_info)
{
    zend_execute_data *exec_frame, *prev_frame;
    zend_array *p_symbol_table;

    exec_frame = EG(current_execute_data);

    // Skipping the frame of the meminfo_dump() function call
    // exec_frame = exec_frame->prev_execute_data;

    while (exec_frame) {
        // zend_rebuild_symbol_table skips non-user frames, but that's where iterator wrappers
        // are hiding, so we need to manually navigate the exec_frames. Copying the top of
        // zend_rebuild_symbol_table fn, but stopping at both user funcctions and iterator wrappers 
        while (exec_frame) {
            meminfo_build_frame_label(stream_info->frame_label, sizeof(stream_info->frame_label), exec_frame);
            if (exec_frame->func && ZEND_USER_CODE(exec_frame->func->common.type)) {
                break;
            }
            exec_frame = exec_frame->prev_execute_data;
        }
        
        // Switch the active frame to the current browsed one and rebuild the symbol table
        // to get it right
        EG(current_execute_data) = exec_frame;

        // copy variables from ex->func->op_array.vars into the symbol table for the last called *user* function
        // therefore it does necessary returns the symbol table of the current frame 
        p_symbol_table = zend_rebuild_symbol_table();

        if (p_symbol_table != NULL) {

            if (exec_frame->prev_execute_data) {
                meminfo_build_frame_label(stream_info->frame_label, sizeof(stream_info->frame_label), exec_frame);
            } else {
                snprintf(stream_info->frame_label, sizeof(stream_info->frame_label), "<GLOBAL>");
            }

            meminfo_browse_zvals_from_symbol_table(stream_info, p_symbol_table);

        }
        exec_frame = exec_frame->prev_execute_data;
    }
}

/**
 * Go through static members of classes
 */
void meminfo_browse_class_static_members(meminfo_stream_info *stream_info)
{
    HashPosition ce_pos;
    HashPosition prop_pos;
    zend_class_entry *class_entry;
    zend_property_info * prop_info;

    char symbol_name[500];
    const char *prop_name, *class_name;
    zend_string * zstr_symbol_name;
    zval * prop;

    zend_hash_internal_pointer_reset_ex(CG(class_table), &ce_pos);
    while ((class_entry = zend_hash_get_current_data_ptr_ex(CG(class_table), &ce_pos)) != NULL) {

#if PHP_VERSION_ID >= 70400
        if (class_entry->default_static_members_count > 0 && CE_STATIC_MEMBERS(class_entry)) {
#else
        if (class_entry->static_members_table) {
#endif

            snprintf(stream_info->frame_label, sizeof(stream_info->frame_label), "<CLASS_STATIC_MEMBER>");
            HashTable *properties_info = &(class_entry->properties_info);

            zend_hash_internal_pointer_reset_ex(properties_info, &prop_pos);

            while ((prop_info = zend_hash_get_current_data_ptr_ex(properties_info, &prop_pos)) != NULL) {

                if (prop_info->flags & ZEND_ACC_STATIC) {
#if PHP_VERSION_ID >= 70400
                    prop = CE_STATIC_MEMBERS(class_entry) + prop_info->offset;
#else
                    prop = &class_entry->static_members_table[prop_info->offset];
#endif

                    zend_unmangle_property_name(prop_info->name, &class_name, &prop_name);

                    if (class_name) {
                        snprintf(symbol_name, sizeof(symbol_name), "%s::%s",  class_name, prop_name);
                    } else {
                        snprintf(symbol_name, sizeof(symbol_name), "%s::%s",  ZSTR_VAL(class_entry->name), ZSTR_VAL(prop_info->name));
                    }

                    zstr_symbol_name = zend_string_init(symbol_name, strlen(symbol_name), 0);

                    meminfo_zval_dump(stream_info, stream_info->frame_label, zstr_symbol_name, prop);

                    zend_string_release(zstr_symbol_name);
                }

                zend_hash_move_forward_ex(properties_info, &prop_pos);
            }
        }
        
        // Static local variables can be hiding in class member functions. Find them here
        meminfo_browse_function_static_variables(
            stream_info,
            ZSTR_VAL(class_entry->name),
            &class_entry->function_table
        );

        zend_hash_move_forward_ex(CG(class_table), &ce_pos);
    }
}

void meminfo_browse_object_store(meminfo_stream_info *stream_info)
{
	zend_object **obj_ptr, **end, *obj;
	zend_class_entry *class_entry;
	zval *closure_used_var;
	HashTable *closure_used_vars;
	HashPosition cov_pos;
	zend_string *current_varname;

	if (EG(objects_store).top <= 1) {
                return;
        }

        end = EG(objects_store).object_buckets + 1;
        obj_ptr = EG(objects_store).object_buckets + EG(objects_store).top;

        do {
 		char zval_identifier[16];
		obj_ptr--;
		obj = *obj_ptr;

		// There might be some references to destructed objects hanging around.
		if (!IS_OBJ_VALID(obj)) {
			continue;
		}

       		sprintf(zval_identifier, "%p", obj);

    		zval isset;
    		zend_string * zstr_item_identifier;

    		zstr_item_identifier = zend_string_init(zval_identifier, strlen(zval_identifier), 0);
    		isset.value.lval = 1;

    		if (!zend_hash_exists(stream_info->visited_items, zstr_item_identifier)) {
			php_printf("Found unseen alive object: %s #%d (%s)\n", obj->ce->name->val, obj->handle, zval_identifier);

			HashTable *properties;
			int is_temp;
			zend_string * escaped_class_name;

			properties = NULL;

			escaped_class_name = meminfo_escape_for_json(ZSTR_VAL(obj->ce->name));

			php_stream_printf(stream_info->stream TSRMLS_CC, "} ,\n");
			php_stream_printf(stream_info->stream TSRMLS_CC, "\"%s\":	{\n", zval_identifier);
			php_stream_printf(stream_info->stream TSRMLS_CC, "        \"class\" : \"%s\",\n", ZSTR_VAL(escaped_class_name));

			zend_string_release(escaped_class_name);
			php_stream_printf(stream_info->stream TSRMLS_CC, "        \"is_root\" : false,\n");
			php_stream_printf(stream_info->stream TSRMLS_CC, "        \"frame\" : \"<OBJECTS_IN_OBJECT_STORE>\",\n");
			php_stream_printf(stream_info->stream TSRMLS_CC, "        \"object_handle\" : \"%d\",\n", obj->handle);

 			php_stream_printf(stream_info->stream TSRMLS_CC, "        \"type\" : \"object\",\n");
    			php_stream_printf(stream_info->stream TSRMLS_CC, "        \"size\" : \"%ld\"\n", sizeof(zend_object));
			//meminfo_hash_dump(stream_info->stream, obj->properties, 1, visited_items, first_element);
			

   		}
 
		zend_string_release(zstr_item_identifier);

	} while (obj_ptr != end);
}


/**
 * Go through static variables of functions
 */
void meminfo_browse_function_static_variables(meminfo_stream_info *stream_info, char* class_name, HashTable *function_table)
{
    char symbol_name[500];
    zend_string * zstr_symbol_name;
    zend_function * func;
    zval * zfunc;
    zend_string * zstaticvarkey;
    zval * zstaticvar;

    ZEND_HASH_FOREACH_VAL(function_table, zfunc) {
        func = Z_FUNC_P(zfunc);
        if (func->type == ZEND_USER_FUNCTION && func->op_array.static_variables != NULL) {
            ZEND_HASH_FOREACH_STR_KEY_VAL(func->op_array.static_variables, zstaticvarkey, zstaticvar) {
                
                snprintf(stream_info->frame_label, sizeof(stream_info->frame_label), "<STATIC_VARIABLE(%s::%s)>",
                    class_name,
                    ZSTR_VAL(func->op_array.function_name)
                );
                snprintf(symbol_name, sizeof(symbol_name), "$%s", ZSTR_VAL(zstaticvarkey));
            
                zstr_symbol_name = zend_string_init(symbol_name, strlen(symbol_name), 0);

                meminfo_zval_dump(stream_info, stream_info->frame_label, zstr_symbol_name, zstaticvar);

                zend_string_release(zstr_symbol_name);
                
            } ZEND_HASH_FOREACH_END();
        }
    } ZEND_HASH_FOREACH_END();
}

void meminfo_browse_handlers(meminfo_stream_info *stream_info)
{
    // First get the current error/exception handler
    snprintf(stream_info->frame_label, sizeof(stream_info->frame_label), "<ERROR_HANDLER>");
    meminfo_error_or_exception_handler_dump(&EG(user_error_handler), stream_info);
    snprintf(stream_info->frame_label, sizeof(stream_info->frame_label), "<EXCEPTION_HANDLER>");
    meminfo_error_or_exception_handler_dump(&EG(user_exception_handler), stream_info);

    // Then get all the previous error/exception handler
    // Calling set_exception_handler twice doesn't just change the handler, but pushes the old
    // one into a stack that can be restored with restore_exception_handler. They should have
    // used the word 'push' instead of 'set'
    snprintf(stream_info->frame_label, sizeof(stream_info->frame_label), "<PREVIOUS_ERROR_HANDLER>");
    zend_stack_apply_with_argument(
        &EG(user_error_handlers),
        ZEND_STACK_APPLY_TOPDOWN,
        (int (*)(void *, void *)) meminfo_error_or_exception_handler_dump,
        stream_info
    );
    snprintf(stream_info->frame_label, sizeof(stream_info->frame_label), "<PREVIOUS_EXCEPTION_HANDLER>");
    zend_stack_apply_with_argument(
        &EG(user_exception_handlers),
        ZEND_STACK_APPLY_TOPDOWN,
        (int (*)(void *, void *)) meminfo_error_or_exception_handler_dump,
        stream_info
    );

    // Handle all register_shutdown_function()
    snprintf(stream_info->frame_label, sizeof(stream_info->frame_label), "<SHUTDOWN_HANDLER>");
    if (BG(user_shutdown_function_names)) {
        zval *zshutdown_function_entry;
        ZEND_HASH_FOREACH_VAL(BG(user_shutdown_function_names), zshutdown_function_entry) {
            php_shutdown_function_entry *shutdown_function_entry = Z_PTR_P(zshutdown_function_entry);
            for (int i = 0; i < shutdown_function_entry->arg_count; i++) {
                meminfo_zval_dump(stream_info, stream_info->frame_label, NULL, shutdown_function_entry->arguments + i);
            }
        } ZEND_HASH_FOREACH_END();
    }
}

/**
 * This should be called with zend_stack_apply_with_argument
 * we return false because a true value stops zend_stack_apply from looking
 */
int meminfo_error_or_exception_handler_dump(zval *callable, meminfo_stream_info *stream_info)
{
    if (Z_TYPE_P(callable) != IS_UNDEF) {
        meminfo_zval_dump(stream_info, stream_info->frame_label, NULL, callable);
    }
    
    return false;
}

void meminfo_browse_zvals_from_symbol_table(meminfo_stream_info *stream_info, HashTable *p_symbol_table)
int meminfo_browse_function_static_variables(zval* zv, void* return_value TSRMLS_DC)
{
	zend_function* function;
	char zval_id[17];
	
	function = (zend_function*) Z_PTR_P(zv);
	// php_printf("%i) %s\n", function->type, ZSTR_VAL(function->op_array.function_name));
	if (function->type == ZEND_USER_FUNCTION) {
		zval *prop, tmp;
		zend_string *key;
		zend_long h;
		zend_property_info *property_info;

		zval statics;

		array_init(&statics);
		
		ZEND_HASH_FOREACH_KEY_VAL(function->op_array.static_variables, h, key, prop) {
			if (key) {
				zval nameAddressPair;

				array_init(&nameAddressPair);
				add_next_index_zval(&nameAddressPair, prop);
				
				if (Z_TYPE_P(prop) == IS_INDIRECT) {
					prop = Z_INDIRECT_P(prop);
				}
				if (Z_ISREF_P(prop)) {
					ZVAL_DEREF(prop);
				}
				if (Z_TYPE_P(prop) == IS_OBJECT) {
					sprintf(zval_id, "%p", Z_OBJ_P(prop));
				} else {
					sprintf(zval_id, "%p", prop);
				}
				add_next_index_string(&nameAddressPair, zval_id);
				
				add_assoc_zval(&statics, ZSTR_VAL(key), &nameAddressPair);
			}
		} ZEND_HASH_FOREACH_END();

		add_assoc_zval((zval*) return_value, ZSTR_VAL(function->op_array.function_name), &statics);
	}
	return 0;
}

int meminfo_browse_function_static(zval* zv, void* return_value TSRMLS_DC)
{
	zend_class_entry* class;
	
	class = (zend_class_entry*) Z_PTR_P(zv);
	// php_printf("%i) %s\n", class->type, ZSTR_VAL(class->name));
	if (class->type == ZEND_USER_CLASS) {
		zval *prop, tmp;
		zend_string *key;
		zend_long h;
		zend_property_info *property_info;

		zval statics;

		array_init(&statics);
		
		zend_hash_apply_with_argument(&class->function_table, meminfo_browse_function_static_variables, return_value TSRMLS_CC);
	}
	return 0;
}

void meminfo_browse_zvals_from_symbol_table(php_stream *stream, char* frame_label, HashTable *p_symbol_table, HashTable * visited_items, int *first_element)
{
    zval *zval_to_dump;
    HashPosition pos;

    zend_string *key;
    zend_long index;

    zend_hash_internal_pointer_reset_ex(p_symbol_table, &pos);

    while ((zval_to_dump = zend_hash_get_current_data_ex(p_symbol_table, &pos)) != NULL) {

        zend_hash_get_current_key_ex(p_symbol_table, &key, &index, &pos);

        meminfo_zval_dump(stream_info, stream_info->frame_label, key, zval_to_dump);

        zend_hash_move_forward_ex(p_symbol_table, &pos);
    }
}

int meminfo_visit_item(char * item_identifier, HashTable *visited_items)
{
    int found = 0;
    zval isset;
    zend_string * zstr_item_identifier;

    zstr_item_identifier = zend_string_init(item_identifier, strlen(item_identifier), 0);

    ZVAL_LONG(&isset, 1);

    if (zend_hash_exists(visited_items, zstr_item_identifier)) {
        found = 1;
    } else {
        zend_hash_add(visited_items, zstr_item_identifier, &isset);
    }
    zend_string_release(zstr_item_identifier);

    return found;
}

void meminfo_hash_dump(meminfo_stream_info *stream_info, HashTable *ht, zend_bool is_object)
{
    zval *zval;

    zend_string *key;
    HashPosition pos;
    zend_ulong num_key;

    int first_child = 1;

    php_stream_printf(stream_info->stream, "        \"children\" : {\n");

    zend_hash_internal_pointer_reset_ex(ht, &pos);
    while ((zval = zend_hash_get_current_data_ex(ht, &pos)) != NULL) {
        char zval_id[17];

        if (Z_TYPE_P(zval) == IS_INDIRECT) {
            zval = Z_INDIRECT_P(zval);
        }

        if (Z_ISREF_P(zval)) {
            ZVAL_DEREF(zval);
        }

        if (Z_TYPE_P(zval) == IS_OBJECT) {
            sprintf(zval_id, "%p", Z_OBJ_P(zval));
        } else {
            sprintf(zval_id, "%p", zval);
        }

        if (!first_child) {
            php_stream_printf(stream_info->stream, ",\n");
        } else {
            first_child = 0;
        }

        switch (zend_hash_get_current_key_ex(ht, &key, &num_key, &pos)) {
            case HASH_KEY_IS_STRING:

                if (is_object) {
                    const char *property_name, *class_name;
                    zend_string * escaped_property_name;

                    zend_unmangle_property_name(key, &class_name, &property_name);

                    escaped_property_name = meminfo_escape_for_json(property_name);

                    php_stream_printf(stream_info->stream, "            \"%s\":\"%s\"", ZSTR_VAL(escaped_property_name), zval_id);

                    zend_string_release(escaped_property_name);
                } else {
                    zend_string * escaped_key;

                    escaped_key = meminfo_escape_for_json(ZSTR_VAL(key));

                    php_stream_printf(stream_info->stream, "            \"%s\":\"%s\"", ZSTR_VAL(escaped_key), zval_id);

                    zend_string_release(escaped_key);
                }

                break;
            case HASH_KEY_IS_LONG:
                php_stream_printf(stream_info->stream, "            \"%ld\":\"%s\"", num_key, zval_id);
                break;
        }

        zend_hash_move_forward_ex(ht, &pos);
    }
    php_stream_printf(stream_info->stream, "\n        }\n");

    zend_hash_internal_pointer_reset_ex(ht, &pos);
    while ((zval = zend_hash_get_current_data_ex(ht, &pos)) != NULL) {
        meminfo_zval_dump(stream_info, NULL, NULL, zval);
        zend_hash_move_forward_ex(ht, &pos);
    }
}

void meminfo_closure_dump(meminfo_stream_info *stream_info, zval *zv)
{
    // zval *zval;

    // zend_string *key;
    // HashPosition pos;
    // zend_ulong num_key;

    int first_child = 1;
    
    // char symbol_name[500];
    // zend_string * zstr_symbol_name;
    // zend_function * func;
    // zval * zfunc;
    zend_string * zstaticvarkey;
    zval * zstaticvar;
    
    const zend_function *closure_func;
    const zval *closure_this;
    
    php_stream_printf(stream_info->stream, "        \"children\" : {\n");
    
    closure_func = zend_get_closure_method_def(zv);
    if (closure_func->op_array.static_variables != NULL) {
        ZEND_HASH_FOREACH_STR_KEY_VAL(closure_func->op_array.static_variables, zstaticvarkey, zstaticvar) {
            char zval_id[17];

            if (Z_TYPE_P(zstaticvar) == IS_INDIRECT) {
                zstaticvar = Z_INDIRECT_P(zstaticvar);
            }

            if (Z_ISREF_P(zstaticvar)) {
                ZVAL_DEREF(zstaticvar);
            }

            if (Z_TYPE_P(zstaticvar) == IS_OBJECT) {
                sprintf(zval_id, "%p", Z_OBJ_P(zstaticvar));
            } else {
                sprintf(zval_id, "%p", zstaticvar);
            }
            
            if (!first_child) {
                php_stream_printf(stream_info->stream, ",\n");
            } else {
                first_child = 0;
            }
            
            php_stream_printf(stream_info->stream, "            \"%s\":\"%s\"", ZSTR_VAL(zstaticvarkey), zval_id);
        } ZEND_HASH_FOREACH_END();
    }
    
    closure_this = zend_get_closure_this_ptr(zv);
    if (Z_TYPE_P(closure_this) != IS_UNDEF) {
        char zval_id[17];

        if (Z_TYPE_P(closure_this) == IS_INDIRECT) {
            closure_this = Z_INDIRECT_P(closure_this);
        }

        if (Z_ISREF_P(closure_this)) {
            ZVAL_DEREF(closure_this);
        }

        if (Z_TYPE_P(closure_this) == IS_OBJECT) {
            sprintf(zval_id, "%p", Z_OBJ_P(closure_this));
        } else {
            sprintf(zval_id, "%p", closure_this);
        }
            
        if (!first_child) {
            php_stream_printf(stream_info->stream, ",\n");
        } else {
            first_child = 0;
        }
        
        php_stream_printf(stream_info->stream, "            \"this\":\"%s\"", zval_id);
    }
    
    php_stream_printf(stream_info->stream, "\n        }\n");
    
    if (closure_func->op_array.static_variables != NULL) {
        ZEND_HASH_FOREACH_VAL(closure_func->op_array.static_variables, zstaticvar) {
            meminfo_zval_dump(stream_info, NULL, NULL, zstaticvar);
        } ZEND_HASH_FOREACH_END();
    }
    
    closure_this = zend_get_closure_this_ptr(zv);
    if (Z_TYPE_P(closure_this) != IS_UNDEF) {
        meminfo_zval_dump(stream_info, NULL, NULL, closure_this);
    }
    
    // zval *zval;

    // zend_string *key;
    // HashPosition pos;
    // zend_ulong num_key;

    // int first_child = 1;

    // php_stream_printf(stream_info->stream, "        \"children\" : {\n");

    // zend_hash_internal_pointer_reset_ex(ht, &pos);
    // while ((zval = zend_hash_get_current_data_ex(ht, &pos)) != NULL) {
    //     char zval_id[17];

    //     if (Z_TYPE_P(zval) == IS_INDIRECT) {
    //         zval = Z_INDIRECT_P(zval);
    //     }

    //     if (Z_ISREF_P(zval)) {
    //         ZVAL_DEREF(zval);
    //     }

    //     if (Z_TYPE_P(zval) == IS_OBJECT) {
    //         sprintf(zval_id, "%p", Z_OBJ_P(zval));
    //     } else {
    //         sprintf(zval_id, "%p", zval);
    //     }

    //     if (!first_child) {
    //         php_stream_printf(stream_info->stream, ",\n");
    //     } else {
    //         first_child = 0;
    //     }

    //     switch (zend_hash_get_current_key_ex(ht, &key, &num_key, &pos)) {
    //         case HASH_KEY_IS_STRING:

    //             if (is_object) {
    //                 const char *property_name, *class_name;
    //                 zend_string * escaped_property_name;

    //                 zend_unmangle_property_name(key, &class_name, &property_name);

    //                 escaped_property_name = meminfo_escape_for_json(property_name);

    //                 php_stream_printf(stream_info->stream, "            \"%s\":\"%s\"", ZSTR_VAL(escaped_property_name), zval_id);

    //                 zend_string_release(escaped_property_name);
    //             } else {
    //                 zend_string * escaped_key;

    //                 escaped_key = meminfo_escape_for_json(ZSTR_VAL(key));

    //                 php_stream_printf(stream_info->stream, "            \"%s\":\"%s\"", ZSTR_VAL(escaped_key), zval_id);

    //                 zend_string_release(escaped_key);
    //             }

    //             break;
    //         case HASH_KEY_IS_LONG:
    //             php_stream_printf(stream_info->stream, "            \"%ld\":\"%s\"", num_key, zval_id);
    //             break;
    //     }

    //     zend_hash_move_forward_ex(ht, &pos);
    // }
    // php_stream_printf(stream_info->stream, "\n        }\n");

    // zend_hash_internal_pointer_reset_ex(ht, &pos);
    // while ((zval = zend_hash_get_current_data_ex(ht, &pos)) != NULL) {
    //     meminfo_zval_dump(stream_info, NULL, NULL, zval);
    //     zend_hash_move_forward_ex(ht, &pos);
    // }
}

bool extra = false;

void meminfo_zval_dump(meminfo_stream_info *stream_info, char * frame_label, zend_string * symbol_name, zval * zv)
{
    char zval_identifier[17];
    bool first_element;
    int count, count_static_props = 0, count_static_funcs = 0, count_shadow_props = 0;

    if (Z_TYPE_P(zv) == IS_INDIRECT) {
        zv = Z_INDIRECT_P(zv);
    }

    if (Z_ISREF_P(zv)) {
        ZVAL_DEREF(zv);
    }

    if (Z_TYPE_P(zv) == IS_OBJECT) {
        sprintf(zval_identifier, "%p", Z_OBJ_P(zv));
    } else {
        sprintf(zval_identifier, "%p", zv);
    }

    first_element = zend_array_count(stream_info->visited_items) > 0;

    if (meminfo_visit_item(zval_identifier, stream_info->visited_items)) {
        return;
    }
    
    if (extra) {
        php_stream_printf(stream_info->stream, " ");
    }

    if (first_element) {
        php_stream_printf(stream_info->stream, "\n    },\n");
    }

    php_stream_printf(stream_info->stream, "    \"%s\" : {\n", zval_identifier);
    php_stream_printf(stream_info->stream, "        \"type\" : \"%s\",\n", zend_get_type_by_const(Z_TYPE_P(zv)));
    php_stream_printf(stream_info->stream, "        \"size\" : \"%ld\",\n", meminfo_get_element_size(zv));

    if (frame_label) {
        zend_string * escaped_frame_label;

        if (symbol_name) {
            zend_string * escaped_symbol_name;

            escaped_symbol_name = meminfo_escape_for_json(ZSTR_VAL(symbol_name));

            php_stream_printf(stream_info->stream, "        \"symbol_name\" : \"%s\",\n", ZSTR_VAL(escaped_symbol_name));

            zend_string_release(escaped_symbol_name);
        }

        escaped_frame_label = meminfo_escape_for_json(frame_label);

        php_stream_printf(stream_info->stream, "        \"is_root\" : true,\n");
        php_stream_printf(stream_info->stream, "        \"frame\" : \"%s\"\n", ZSTR_VAL(escaped_frame_label));

        zend_string_release(escaped_frame_label);
    } else {
        php_stream_printf(stream_info->stream, "        \"is_root\" : false\n");
    }

    if (Z_TYPE_P(zv) == IS_OBJECT) {
        HashTable *properties;
        zend_string * escaped_class_name;

        properties = NULL;

        escaped_class_name = meminfo_escape_for_json(ZSTR_VAL(Z_OBJCE_P(zv)->name));

        php_stream_printf(stream_info->stream, ",\n");
        if (strcmp(ZSTR_VAL(escaped_class_name), "Carbon\\\\Translator") == 0) {
            php_stream_printf(stream_info->stream, "        \"isTranslator\" : \"true\",\n");
            
            
        }
        php_stream_printf(stream_info->stream, "        \"class\" : \"%s\",\n", ZSTR_VAL(escaped_class_name));

        zend_string_release(escaped_class_name);

        php_stream_printf(stream_info->stream, "        \"object_handle\" : \"%d\",\n", Z_OBJ_HANDLE_P(zv));

// #if PHP_VERSION_ID >= 70400
//         properties = zend_get_properties_for(zv, ZEND_PROP_PURPOSE_DEBUG);
// #else
//         int is_temp;
//         properties = Z_OBJDEBUG_P(zv, is_temp);
// #endif

//         if (properties != NULL) {
//             meminfo_hash_dump(stream_info, properties, 1);

// #if PHP_VERSION_ID >= 70400
//             zend_release_properties(properties);
// #else
//             if (is_temp) {
//                 zend_hash_destroy(properties);
//                 efree(properties);
//             }
// #endif
//         }

        // Closures have special memory properties
        if (instanceof_function(Z_OBJCE_P(zv), zend_ce_closure)) {
            meminfo_closure_dump(stream_info, zv);
        } else {
            meminfo_hash_dump(stream_info, Z_OBJ_HT_P(zv)->get_properties(zv), 1);
        }
        
    } else if (Z_TYPE_P(zv) == IS_ARRAY) {
        php_stream_printf(stream_info->stream, ",\n");
        meminfo_hash_dump(stream_info, Z_ARRVAL_P(zv), 0);
    } else {
        php_stream_printf(stream_info->stream, "\n");
    }
}

/**
 * Get size of an element
 *
 * @param zval *zv Zval of the element
 *
 * @return zend_ulong
 */
zend_ulong meminfo_get_element_size(zval *zv)
{
    zend_ulong size;

    size = sizeof(zval);

    switch (Z_TYPE_P(zv)) {
        case IS_STRING:
            size += Z_STRLEN_P(zv);
            break;

        // TODO: add size of the indexes
        case IS_ARRAY:
            size += sizeof(HashTable);
            break;

        // TODO: add size of the properties table, but without property content
        case IS_OBJECT:
            size += sizeof(zend_object);
            break;
    }

    return size;
}

/**
 * Build the current frame label based on function name and object class
 * if necessary.
 *
 * Most code comes from the debug_print_backtrace implementation.
 */
void meminfo_build_frame_label(char* frame_label, int frame_label_len, zend_execute_data* frame)
{
    zend_function *func;
    const char *function_name;
    char * call_type;
    zend_string *class_name = NULL;
    zend_object *object;
    zend_execute_data *ptr;

    object = Z_OBJ(frame->This);
    ptr = frame->prev_execute_data;

    if (frame->func) {
        func = frame->func;

#if PHP_VERSION_ID >= 80000
        if (func->common.function_name) {
            function_name = ZSTR_VAL(func->common.function_name);
        } else {
            function_name = NULL;
        }
#else
        function_name = (func->common.scope &&
                         func->common.scope->trait_aliases) ?
            ZSTR_VAL(zend_resolve_method_name(
                (object ? object->ce : func->common.scope), func)) :
            (func->common.function_name ?
                ZSTR_VAL(func->common.function_name) : NULL);
#endif
    } else {
        func = NULL;
        function_name = NULL;
    }

    if (function_name) {
        if (object) {
            if (func->common.scope) {
                class_name = func->common.scope->name;
            } else {
                class_name = object->ce->name;
            }

            call_type = "->";
        } else if (func->common.scope) {
            class_name = func->common.scope->name;
            call_type = "::";
        } else {
            class_name = NULL;
            call_type = NULL;
        }
    } else {

        if (!ptr || !ptr->func || !ZEND_USER_CODE(ptr->func->common.type) || ptr->opline->opcode != ZEND_INCLUDE_OR_EVAL) {
            /* can happen when calling eval from a custom sapi */
            function_name = "unknown";
        } else
        switch (ptr->opline->extended_value) {
            case ZEND_EVAL:
                function_name = "eval";
                break;
            case ZEND_INCLUDE:
                function_name = "include";
                break;
            case ZEND_REQUIRE:
                function_name = "require";
                break;
            case ZEND_INCLUDE_ONCE:
                function_name = "include_once";
                break;
            case ZEND_REQUIRE_ONCE:
                function_name = "require_once";
                break;
            default:
                /* this can actually happen if you're in your error_handler and
                 * you're in the top-scope */
                function_name = "unknown";
                break;
        }
    }
    if (class_name) {
        snprintf(frame_label, frame_label_len, "%s%s%s()", ZSTR_VAL(class_name), call_type, function_name);
    } else {
        snprintf(frame_label, frame_label_len, "%s()", function_name);
    }
}

/**
 * Escape for JSON encoding
 */
zend_string * meminfo_escape_for_json(const char *s)
{
    int i;
    char unescaped_char[2];
    char escaped_char[7]; // \uxxxx format
    zend_string *s1, *s2, *s3 = NULL;

    s1 = php_str_to_str((char *) s, strlen(s), "\\", 1, "\\\\", 2);
    s2 = php_str_to_str(ZSTR_VAL(s1), ZSTR_LEN(s1), "\"", 1, "\\\"", 2);

    for (i = 0; i <= 0x1f; i++) {
        unescaped_char[0] =  (char) i;
        sprintf(escaped_char, "\\u%04x", i);
        if (s3) {
            s2 = s3;
        }
        s3 = php_str_to_str(ZSTR_VAL(s2), ZSTR_LEN(s2), unescaped_char, 1, escaped_char, 6);
        zend_string_release(s2);
    }

    zend_string_release(s1);

    return s3;
}

#ifdef COMPILE_DL_MEMINFO
#ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE();
#endif
ZEND_GET_MODULE(meminfo)
#endif
