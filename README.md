# JSON C Library

## Description

JSON C library is fast, lightweight JSON parsing, output and dynamic data structure forming library.  
Warning: this library is still under development.

## Usage



## Data Structures

JSON C library forms 2 structures object and value.  

Object       Value  
+-------+    +-------+  
|name   |    |type   |  
|value -+--->|value*-+--->Object or Value  
|next   |    |next   |  
+--+----+    +--+----+  
   |            |  
   V            V  
Object        Value  

Object:  
```
struct json_object{
    size_t n;
    char *name;
    struct json_value *value;
    struct json_object *next;
};
```
Value:  
```
struct json_value{
    uint8_t type;
    size_t n;
    union{
	char *value_string;
	int64_t value_int;
	double value_double;
	uint8_t value_boolean;
	struct json_object *object;
	struct json_value *value;
    };
    struct json_value *next;
};
```

Value types  
0 No data,  
1 object,  
2 array/value,  
3 string,  
4 integer,  
5 double,  
6 boolean/null.  


JSON example:  
```
{ "name" : "value" }
```
Object       Value  
+-------+    +-------+  
|name   |    |type 3 |  
|value -+--->|value  |  
|next   |    |next   |  
+--+----+    +--+----+  
   |            |  
   V            V  
  NULL         NULL  


JSON example:  
```
{
  "name1" : "value1",
  "name2" : "value2"
}
```
Object       Value  
+-------+    +-------+  
|name1  |    |type 3 |  
|value -+--->|value1 |  
|next   |    |next   |  
+--+----+    +--+----+  
   |            |  
   |            V  
   |           NULL  
   V  
Object       Value  
+-------+    +-------+  
|name2  |    |type 3 |  
|value -+--->|value2 |  
|next   |    |next   |  
+--+----+    +--+----+  
   |            |  
   V            V  
  NULL         NULL  


JSON example:  
```
{ "name" : ["value1","value2"] }
```
Object       Value  
+-------+    +-------+  
|name   |    |type 3 |  
|value -+--->|value1 |  
|next   |    |next   |  
+--+----+    +--+----+  
   |            |  
   V            V  
  NULL       Value  
             +-------+  
             |type 3 |  
             |value3 |  
             |next   |  
             +--+----+  
                |  
                V  
              NULL  


JSON example:  
```
{ "name" : [["value1","value2"],"value3"] }
```
Object       Value         Value  
+-------+    +-------+     +-------+  
|name   |    |type 2 |     |type 3 |  
|value -+--->|value -+---> |value1 |  
|next   |    |next   |     |next   |  
+--+----+    +--+----+     +--+----+  
   |            |             |
   V            V             V
  NULL       Value        Value  
             +-------+    +-------+  
             |type 3 |    |type 3 |  
             |value3 |    |value2 |  
             |next   |    |next   |  
             +--+----+    +--+----+  
                |            |  
                V            V  
               NULL         NULL  


JSON example:  
```
{ "name1" : {"name2":"value"} }
```
Object       Value        Object       Value  
+-------+    +-------+    +-------+    +-------+  
|name1  |    |type 1 |    |name2  |    |type 3 |  
|value -+--->|value -+--->|value -+--->|value  |  
|next   |    |next   |    |next   |    |next   |  
+--+----+    +--+----+    +--+----+    +--+----+  
   |            |            |  
   V            V            V  
  NULL         NULL         NULL  

