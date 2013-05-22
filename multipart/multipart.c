#include <Python.h>
#include "multipart_parser.h"
#include <structmember.h>
#include "iso646.h"
#include "stdbool.h"
#include "stdio.h"
#include "string.h"

static PyObject * multipartModule = NULL;

typedef struct {
	PyObject_HEAD
	multipart_parser * parser;
	
	//The field of the header currently being parsed
	char * headerFieldInProgress;
	size_t headerFieldLength;
	size_t headerFieldSize;
	
	//The value of the header currently being parsed
	char * headerValueInProgress;
	size_t headerValueLength;
	size_t headerValueSize;
	
	char * data;
	
	//The callable object that reads data
	PyObject * readIterator;
	//The number of bytes read from the iterator
	size_t bytesParsed;

	//Set to true if currently in the body of a part
	bool headersComplete;
	//Set to true if done with the multipart
	bool dataComplete;

	PyObject ** iteratorQueue;
	size_t iteratorQueueLengthInPairs;
	size_t iteratorQueueSizeInPairs;
	ssize_t currentIteratorPair;
	ssize_t outgoingIteratorPair;
	
} multipart_Parser;



static PyObject* Parser_new(PyTypeObject * type, PyObject * args, PyObject * kwds)
{
	multipart_Parser * self = (multipart_Parser*)type->tp_alloc(type,0);
	
	if(self!=NULL)
	{
		self->iteratorQueue = NULL;
		self->iteratorQueueLengthInPairs = 0;
		self->iteratorQueueSizeInPairs = 0;
		self->currentIteratorPair = -1;
		self->outgoingIteratorPair = 0;
		
		self->parser = NULL;
		self->bytesParsed = 0;

		self->headersComplete = true;
		self->dataComplete = false;
		self->readIterator = NULL;
		static const int STARTING_SIZE = 2;
		self->headerFieldInProgress = PyMem_Malloc(STARTING_SIZE*sizeof(char));
		self->headerFieldLength = 0;
		self->headerFieldSize = STARTING_SIZE;
		
		self->headerValueInProgress = PyMem_Malloc(STARTING_SIZE*sizeof(char));
		self->headerValueLength = 0;
		self->headerValueSize = STARTING_SIZE;
		
		//If memory allocation fails in either case,
		//give up.
		if( self->headerFieldInProgress == NULL or self->headerValueInProgress == NULL)
		{
			PyMem_Free(self->headerFieldInProgress);
			PyMem_Free(self->headerValueInProgress);
			PyMem_Free(self);
			return PyErr_NoMemory();
		}		
	}
	
	return (PyObject*)self;
}

static void Parser_dealloc(multipart_Parser * self)
{
	if(self->parser)
	{
		multipart_parser_free(self->parser);
	}
	
	PyMem_Free(self->headerFieldInProgress);
	PyMem_Free(self->headerValueInProgress);
	
	Py_XDECREF(self->readIterator);
	
	for(size_t i = 0;i < self->iteratorQueueLengthInPairs ; i+=2)
	{
		Py_XDECREF(self->iteratorQueue[i]);
		Py_XDECREF(self->iteratorQueue[i+1]);
	}
	
	PyMem_Free(self->iteratorQueue);
}
  
static bool queuePush(multipart_Parser * const self)
{
	self->currentIteratorPair += 1;
	//TODO check for exceeding bounds of queue
	
	PyObject * const generatorObject = PyObject_GetAttrString(multipartModule,"Generator");
	
	if(not generatorObject)
	{
		PyErr_SetString(PyExc_NameError,"Cannot find multipart.Generator");
		return false;
	}
	
	PyObject * const read = PyObject_GetAttrString((PyObject*)self,"read");
	
	if(not read)
	{
		PyErr_SetString(PyExc_NameError,"Could not find self.read");
		return false;
	}
	
	PyObject * const argument = PyTuple_Pack(1,read);
	Py_DECREF(read);
	
	PyObject * const headerIterator = PyObject_Call(generatorObject,argument,NULL);
	PyObject * const bodyIterator = PyObject_Call(generatorObject,argument,NULL);
	Py_DECREF(argument);
	Py_DECREF(generatorObject);
	
	if(not headerIterator or not bodyIterator)
	{	
		Py_XDECREF(headerIterator);
		Py_XDECREF(bodyIterator);
		
		self->currentIteratorPair-=1;
		
		PyErr_NoMemory();
		return false;
	}
	
	self->iteratorQueue[self->currentIteratorPair*2] = headerIterator;
	self->iteratorQueue[self->currentIteratorPair*2+1] = bodyIterator;
	
	return true;
}
  
static int multipart_Parser_on_header_field(void * actor, const char * data, size_t length)
{
	printf("header field %zu bytes:%.*s\n",length,(int)length,data);
	multipart_Parser * const self = actor;
	
	//Check for this being headers on a new part
	if(self->headersComplete)
	{
		if(not queuePush(self))
		{
			return 1;
		}
		self->headersComplete = false;
	}
	
	//Calculate the required size after adding the new data
	const int requiredSize = length + self->headerFieldLength + 1;
	
	//Reallocate memory if neccessary
	if(requiredSize > self->headerFieldSize)
	{
		void * const newMem = PyMem_Realloc(self->headerFieldInProgress,requiredSize);
		if(not newMem)
		{
			//Return -1 to indicate failure in callback to the parser.
			return -1;
		}
		self->headerFieldInProgress = newMem;
		self->headerFieldSize = requiredSize;
	}
	
	//Copy the memory onto the field and update the length;
	memcpy(self->headerFieldInProgress + self->headerFieldLength, data,length);
	self->headerFieldLength += length;
	return 0;
}

static int multipart_Parser_on_header_value(void * actor, const char * data, size_t length)
{
	printf("header value %zu bytes:%.*s\n",length,(int)length,data);
	
	multipart_Parser * const self = actor;
	
	const int requiredSize = length + self->headerValueLength + 1;
	
	if(requiredSize > self->headerValueSize)
	{
		void * const newMem = PyMem_Realloc(self->headerValueInProgress,requiredSize);
		if(not newMem)
		{
			return -1;
		}
		
		self->headerValueInProgress = newMem;
		self->headerValueSize = requiredSize;
	}
	
	memcpy(self->headerValueInProgress + self->headerValueLength, data, length);
	self->headerValueLength += length;
	return 0;
}

static int multipart_Parser_on_part_data(void * actor , const char * data, size_t length)
{
	printf("part data %zu bytes:%.*s\n",length,(int)length,data);
	return 0;
}

static int multipart_Parser_on_header_value_end(void * actor)
{
	printf("header_value_end\n");
	multipart_Parser * const self = actor;
	printf("%.*s=%.*s\n",(int)self->headerFieldLength,self->headerFieldInProgress,
	(int)self->headerValueLength,self->headerValueInProgress);
	
	//Null terminate both buffers
	self->headerFieldInProgress[self->headerFieldLength] = '\0';
	self->headerValueInProgress[self->headerValueLength] = '\0';
	
	PyObject * const field = PyString_FromString(self->headerFieldInProgress);
	PyObject * const value = PyString_FromString(self->headerValueInProgress);
	
	if(not value or not field)
	{
		Py_XDECREF(field);
		Py_XDECREF(value);
		PyErr_NoMemory();
		return 1;
	}
	
	PyObject * const tuple = PyTuple_Pack(2,field,value);
	Py_DECREF(field);
	Py_DECREF(value);
	
	if(not tuple)
	{
		PyErr_NoMemory();
		return 1;
	}
	
	PyObject * const push = PyObject_GetAttrString(self->iteratorQueue[self->currentIteratorPair*2],"push");
	
	if(not push)
	{
		PyErr_SetString(PyExc_NameError,"Cannot find Generator.push");
		Py_DECREF(tuple);
		return 1;
	}
	
	PyObject * const result = PyObject_Call(push,tuple,NULL);
	Py_DECREF(tuple);
	Py_DECREF(push);
	
	if(not result)
	{
		return 1;
	}
	
	Py_DECREF(result);
	
	self->headerValueLength = 0;
	self->headerFieldLength = 0;
	return 0;
}

static int multipart_Parser_on_headers_complete(void * actor)
{
	printf("on_headers_complete\n");
	multipart_Parser * const self = actor;
	self->headersComplete = true;
	
	PyObject * const done = PyObject_GetAttrString(self->iteratorQueue[self->currentIteratorPair*2],"done");
	
	if(not done)
	{
		PyErr_SetString(PyExc_NameError,"Cannot find Generator.done");
		return 1;
	}
	
	PyObject * const emptyTuple = PyTuple_Pack(0);
	
	PyObject * const result = PyObject_Call(done,emptyTuple,NULL);
	Py_DECREF(result);
	Py_DECREF(emptyTuple);
	
	if(not result)
	{
		return 1;
	}
	
	return 0;
}

static int multipart_Parser_on_part_data_end(void * actor)
{
	multipart_Parser * const self = actor;
	//self->headersComplete = false;
	
	return 0;
}

static int multipart_Parser_on_body_end(void * actor)
{
	multipart_Parser * const self = actor;
	self->dataComplete = true;
	
	return 0;
}

static multipart_parser_settings callbackRegistry = 
{
  multipart_Parser_on_header_field, //multipart_data_cb on_header_field;
  multipart_Parser_on_header_value, //multipart_data_cb on_header_value;
  multipart_Parser_on_part_data, //multipart_data_cb on_part_data;

  multipart_Parser_on_header_value_end, //multipart_notify_cb on_header_value_end;
  NULL, //multipart_notify_cb on_part_data_begin;
  multipart_Parser_on_headers_complete, //multipart_notify_cb on_headers_complete;
  multipart_Parser_on_part_data_end, //multipart_notify_cb on_part_data_end;
  multipart_Parser_on_body_end //multipart_notify_cb on_body_end;
};

static bool allocateIteratorQueue(multipart_Parser * const self)
{
	static const int STARTING_SIZE = 3;
	const int SIZE_BYTES = sizeof(PyObject*)*STARTING_SIZE*2;
	self->iteratorQueue = PyMem_Malloc(SIZE_BYTES);
	
	if(not self->iteratorQueue)
	{
		PyErr_NoMemory();
		return false;
	}
	bzero(self->iteratorQueue,SIZE_BYTES);
	self->iteratorQueueSizeInPairs = STARTING_SIZE;
	return true;
}

static int Parser_init(multipart_Parser * const self, PyObject * args, PyObject * kwds)
{

	char const * boundary;
	PyObject * fin;
	static char * kwlist[] = {"boundary","fin",NULL};
	if( not PyArg_ParseTupleAndKeywords(args,kwds,"sO",kwlist,&boundary,&fin) )
	{
		return -1;
	}
	
	self->readIterator = PyObject_GetIter(fin);
	
	if(not self->readIterator) 
	{
		PyErr_SetString(PyExc_AttributeError,"fin must be iterable");
		return -1;
	}
	
	self->parser = multipart_parser_init(boundary,&callbackRegistry);
	if( not self->parser )
	{
		PyErr_SetString(PyExc_MemoryError,"multipart_parser_init returned NULL");
		return -1;
	}
	
	multipart_parser_set_data(self->parser,(void*)self);
	
	if(not allocateIteratorQueue(self))
	{
		return -1;
	}

	
	return 0;
}

static PyObject* Parser_iter(PyObject * self)
{
	Py_INCREF(self);
	return self;
}

static PyObject* Parser_read(multipart_Parser * const self, PyObject * unused0, PyObject * unused1)
{
	PyObject * const i = PyIter_Next(self->readIterator);
	
	if(i == NULL)
	{
		Py_RETURN_NONE;
	}
	
	PyObject * const bytes = PyObject_Bytes(i);
	Py_DECREF(i);
	
	if( not bytes )
	{
		PyErr_SetString(PyExc_ValueError,"iterable must return bytes like objects");
		return NULL;
		
	}
	
	char const * raw;
	Py_ssize_t length;
	
	if(-1==PyString_AsStringAndSize(bytes,&raw,&length))
	{
		PyErr_SetString(PyExc_ValueError,"iterable must return bytes like objects");
		Py_DECREF(bytes);
		return NULL;
	}

	const size_t result = multipart_parser_execute(self->parser,raw,length) ;
	self->bytesParsed += result;
	if( length != result )
	{
		char errmsg[64];
		snprintf(errmsg,
				 sizeof(errmsg),
				 "input not multipart, failed on byte %zu",
				 self->bytesParsed);
		errmsg[sizeof(errmsg)-1]='\0';
		
		PyErr_SetString(PyExc_ValueError, errmsg);
		Py_DECREF(bytes);
		return NULL;
	}
	Py_DECREF(bytes);
	
	Py_RETURN_NONE;
}

static PyObject* Parser_iternext(multipart_Parser * const self)
{
	
	if(self->dataComplete)
	{
		return NULL;
	}

	PyObject * const read = PyObject_GetAttrString((PyObject*)self,"read");
	
	if(not read)
	{
		PyErr_SetString(PyExc_NameError,"Could not find self.read");
		return NULL;
	}
	
	if(1!=PyCallable_Check(read))
	{
		PyErr_SetString(PyExc_TypeError,"self.read is not callable");
		return NULL;
	}
	
	//Check to see if the iterator pair being returned is getting ahead
	//of the iterator pair being populated
	
	PyObject * const emptyTuple = PyTuple_New(0);
	
	while(self->outgoingIteratorPair > self->currentIteratorPair)
	{
		printf("Parser_iternext\n");
		if(self->dataComplete)
		{
			Py_DECREF(emptyTuple);
			Py_DECREF(read);
			return NULL;
		}

		PyObject * const callresult = PyObject_Call(read,emptyTuple,NULL);
		
		if(not callresult)
		{
			Py_DECREF(emptyTuple);
			Py_DECREF(read);
			return NULL;
		}
		
		Py_DECREF(callresult);
	}
	
	Py_DECREF(read);
	Py_DECREF(emptyTuple);
	
	PyObject * retval = PyTuple_Pack(2,self->iteratorQueue[self->outgoingIteratorPair*2],self->iteratorQueue[self->outgoingIteratorPair*2+1]);
	
	if(not retval)
	{
		PyErr_SetString(PyExc_RuntimeError,"Failed to build tuple");
		return NULL;
	}
	
	self->outgoingIteratorPair += 1;
	
	return retval;
}

typedef struct
{
	PyObject_HEAD
	PyObject * callback;
	
	PyObject ** queue;
	size_t queueSize;
	size_t queueLength;
	size_t queueRead;
	
	bool done;
}multipart_Generator;

static PyObject * Generator_iter(PyObject * const self)
{
	Py_INCREF(self);
	return self;
}

static PyObject * Generator_iternext(multipart_Generator * const self)
{
	
	PyObject * const emptyTuple = PyTuple_New(0);
	
	while(self->queueRead == self->queueLength)
	{
		if(self->done)
		{
			printf("Generator_iternext-done\n");
			return NULL;
		}
		
		PyObject * const result = PyObject_Call(self->callback,emptyTuple,NULL);
		
		if(not result)
		{
			Py_DECREF(emptyTuple);
			return NULL;
		}
		
		Py_DECREF(result);
		//printf("Generator_iternext\n");
	}
	Py_DECREF(emptyTuple);
	
	PyObject * const retval = self->queue[self->queueRead];
	self->queue[self->queueRead] = NULL;
	self->queueRead += 1;
	
	return retval;

}

static void Generator_dealloc(multipart_Generator * self)
{
	for(size_t i = self->queueRead; i < self->queueLength; ++i)
	{
		Py_DECREF(self->queue[i]);
	}
	
	PyMem_Free(self->queue);
	Py_DECREF(self->callback);
}
static PyObject * Generator_done(multipart_Generator * self, PyObject *args, PyObject *kwds)
{
	printf("Generator_done\n");
	self->done = true;
	Py_RETURN_NONE;
}

static PyObject * Generator_push(multipart_Generator * self, PyObject *args, PyObject *kwds)
{
	PyObject * item;
	
	if(PyTuple_Size(args)==1)
	{
		if( not PyArg_ParseTuple(args,"O",&item) )
		{
			return NULL;
		}
	}
	else
	{
		item = args;
	}
	
	Py_INCREF(item);
	
	const size_t newLength = self->queueLength +1;
	
	if(newLength > self->queueSize)
	{
		if(self->queueRead != 0)
		{
			memmove(self->queue,self->queue + self->queueRead,self->queueLength - self->queueRead  * sizeof(PyObject*));
			self->queueLength -= self->queueRead;
			self->queueRead = 0;
		}
		else
		{
			self->queueSize *= 2;
			const int NEW_SIZE = sizeof(PyObject*)*self->queueSize;
			self->queue = PyMem_Realloc(self->queue,NEW_SIZE);
		}
	}
	
	self->queue[self->queueLength] = item;
	self->queueLength += 1;
	
	Py_RETURN_NONE;
}

static PyMethodDef Generator_methods[] = 
{ 
	{"push",(PyCFunction)Generator_push,METH_KEYWORDS,"push an object into the generator"} ,
	{"done",(PyCFunction)Generator_done,METH_KEYWORDS,"signal the iterator to end the data stream"},
	{NULL} 
};
static PyMemberDef Generator_members[] = { {NULL} };

static int Generator_init(multipart_Generator * self, PyObject *args, PyObject *kwds)
{
	PyObject * callback;
	static char * kwlist[] = {"callback",NULL};
	if( not PyArg_ParseTupleAndKeywords(args,kwds,"O",kwlist,&callback) )
	{
		return -1;
	}
	
	self->callback = callback;
	Py_INCREF(self->callback);
	
	static const int STARTING_SIZE = 4;
	const int SIZE_BYTES = sizeof(PyObject*)*STARTING_SIZE;
	self->queue = PyMem_Malloc(SIZE_BYTES);
	if(not self->queue)
	{
		PyErr_NoMemory();
		return -1;
	}
	bzero(self->queue,SIZE_BYTES);
	
	self->queueSize = STARTING_SIZE;
	self->queueLength = 0;
	self->queueRead = 0;
	self->done = false;
	
	return 0;
}



static PyTypeObject multipart_GeneratorType = {
	PyObject_HEAD_INIT(NULL)
	0,                         /*ob_size*/
    "multipart.CallbackGenerator",             /*tp_name*/
    sizeof(multipart_Generator), /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)Generator_dealloc          ,/*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_HAVE_CLASS | Py_TPFLAGS_HAVE_ITER,        /*tp_flags*/
    "Generator object",           /* tp_doc */
    0,		               /* tp_traverse */
    0,		               /* tp_clear */
    0,		               /* tp_richcompare */
    0,		               /* tp_weaklistoffset */
    Generator_iter,		               /* tp_iter */
    Generator_iternext,		               /* tp_iternext */
    Generator_methods,             /* tp_methods */
    Generator_members,             /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)Generator_init,      /* tp_init */
    0,                         /* tp_alloc */
    0,                 /* tp_new */
    0, /* tp_iter */
    0 //CallbackGenerator_iternext /* tp_iternext */
};

static PyMethodDef Parser_methods[] = { {"read",(PyCFunction)Parser_read, METH_KEYWORDS, "read from input source"},{NULL,NULL,0,NULL} };
static PyMemberDef Parser_members[] = { {NULL} };

static PyTypeObject multipart_ParserType = {
	PyObject_HEAD_INIT(NULL)
	0,                         /*ob_size*/
    "multipart.Parser",             /*tp_name*/
    sizeof(multipart_Parser), /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)Parser_dealloc,/*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_HAVE_CLASS | Py_TPFLAGS_HAVE_ITER,        /*tp_flags*/
    "Parser objects",           /* tp_doc */
    0,		               /* tp_traverse */
    0,		               /* tp_clear */
    0,		               /* tp_richcompare */
    0,		               /* tp_weaklistoffset */
    Parser_iter,		               /* tp_iter */
    Parser_iternext,              /* tp_iternext */
    Parser_methods,             /* tp_methods */
    Parser_members,             /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)Parser_init,      /* tp_init */
    0,                         /* tp_alloc */
    Parser_new                 /* tp_new */

};

static PyMethodDef multipart_methods[] = {
	
	{NULL,NULL,0,NULL}
};



#ifndef PyMODINIT_FUNC	/* declarations for DLL import/export */
#define PyMODINIT_FUNC void
#endif
PyMODINIT_FUNC
initmultipart(void) 
{
    

    multipart_GeneratorType.tp_new = &PyType_GenericNew;
    
    if (PyType_Ready(&multipart_ParserType) < 0 or PyType_Ready(&multipart_GeneratorType) < 0)
    {
        return;
    }

    multipartModule = Py_InitModule3("multipart", multipart_methods,
                       "Example module that creates an extension type.");

    Py_INCREF(&multipart_ParserType);
    
    PyModule_AddObject(multipartModule, "Parser", (PyObject *)&multipart_ParserType);
    PyModule_AddObject(multipartModule, "Generator", (PyObject *)&multipart_GeneratorType);
}
