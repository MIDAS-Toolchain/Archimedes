#ifndef SAVE_DATA_H
#define SAVE_DATA_H

void  save_data_init(void);
void  save_data_set_int(const char* key, int value);
void  save_data_set_float(const char* key, float value);
int   save_data_get_int(const char* key, int default_val);
float save_data_get_float(const char* key, float default_val);
void  save_data_flush(void);
void  save_data_clear_all(void);
long  save_data_get_size(void);

#endif /* SAVE_DATA_H */
