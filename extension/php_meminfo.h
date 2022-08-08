#ifndef PHP_MEMINFO_H
#define PHP_MEMINFO_H 1

extern zend_module_entry meminfo_module_entry;
#define phpext_meminfo_ptr &meminfo_module_entry

#define MEMINFO_NAME "PHP Meminfo"
#define MEMINFO_VERSION "2.0.0-beta1"
#define MEMINFO_AUTHOR "Benoit Jacquemont"
#define MEMINFO_COPYRIGHT  "Copyright (c) 2010-2021 by Benoit Jacquemont & contributors"
#define MEMINFO_COPYRIGHT_SHORT "Copyright (c) 2010-2021"

int meminfo_assign_handler(zend_execute_data *execute_data);
int meminfo_assign_dim_handler(zend_execute_data *execute_data);
int meminfo_assign_obj_handler(zend_execute_data *execute_data);

ZEND_BEGIN_MODULE_GLOBALS(meminfo)
    user_opcode_handler_t original_assign_handler;
    user_opcode_handler_t original_assign_dim_handler;
    user_opcode_handler_t original_assign_obj_handler;
ZEND_END_MODULE_GLOBALS(meminfo)

PHP_MINIT_FUNCTION(meminfo);
PHP_MSHUTDOWN_FUNCTION(meminfo);

#ifdef ZTS
#define MEMINFO_G(v) TSRMG(meminfo_globals_id, zend_meminfo_globals *, v)
#else
#define MEMINFO_G(v) (meminfo_globals.v)
#endif

PHP_FUNCTION(meminfo_dump);

zend_ulong   meminfo_get_element_size(zval* z);

typedef struct meminfo_stream_info {
	php_stream *stream;
	HashTable *visited_items;
	char frame_label[500];
} meminfo_stream_info;

// Functions to browse memory parts to record item
void meminfo_browse_exec_frames(meminfo_stream_info *stream_info);
void meminfo_browse_class_static_members(meminfo_stream_info *stream_info);
void meminfo_browse_function_static_variables(meminfo_stream_info *stream_info, char* class_name, HashTable *function_table);
void meminfo_browse_handlers(meminfo_stream_info *stream_info);

int meminfo_error_or_exception_handler_dump(zval *callable, meminfo_stream_info *stream_info);
void meminfo_zval_dump(meminfo_stream_info *stream_info, char * frame_label, zend_string * symbol_name, zval * zv);
void meminfo_hash_dump(meminfo_stream_info *stream_info, HashTable *ht, zend_bool is_object);
void meminfo_closure_dump(meminfo_stream_info *stream_info, zval *zv);
void meminfo_browse_zvals_from_symbol_table(meminfo_stream_info *stream_info, HashTable *symbol_table);
void meminfo_browse_zvals_from_op_array(meminfo_stream_info *stream_info, zend_op_array *op_array, zend_execute_data *exec_frame);
void meminfo_browse_object_store(meminfo_stream_info *stream_info);
void meminfo_browse_exec_frames(php_stream *stream,  HashTable *visited_items, int *first_element);
void meminfo_browse_class_static_members(php_stream *stream,  HashTable *visited_items, int *first_element);
apply_func_arg_t meminfo_browse_global_function_static_variables;

void meminfo_zval_dump(php_stream * stream, char * frame_label, zend_string * symbol_name, zval * zv, HashTable *visited_items, int *first_element);
void meminfo_hash_dump(php_stream *stream, HashTable *ht, zend_bool is_object, HashTable *visited_items, int *first_element);
void meminfo_browse_zvals_from_symbol_table(php_stream *stream, char * frame_label, HashTable *symbol_table, HashTable * visited_items, int *first_element);
void meminfo_browse_zvals_from_op_array(php_stream *stream, char * frame_label, zend_op_array *op_array, zend_execute_data *exec_frame, HashTable * visited_items, int *first_element);
void meminfo_browse_object_store(php_stream *stream, HashTable * visited_items, int *first_element);

int meminfo_visit_item(char *item_identifier, HashTable *visited_items);

void meminfo_build_frame_label(char * frame_label, int frame_label_len, zend_execute_data* frame);

zend_string * meminfo_escape_for_json(const char *s);

extern zend_module_entry meminfo_entry;

#endif
