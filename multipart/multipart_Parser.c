#include <Python.h>
#include <structmember.h>

#include "multipart.h"
#include "multipart_Parser.h"
#include "iso646.h"
#include "stdbool.h"
#include "multipart_parser.h"

struct multipart_Parser;
typedef struct multipart_Parser multipart_Parser;
struct multipart_Parser {
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
	
} ;

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
		static const int STARTING_SIZE = 3;
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
	
	for(size_t i = 0;i < self->iteratorQueueLengthInPairs ; i++)
	{
		Py_XDECREF(self->iteratorQueue[i*2]);
		Py_XDECREF(self->iteratorQueue[i*2+1]);
	}
	
	PyMem_Free(self->iteratorQueue);
}
  
static bool queuePush(multipart_Parser * const self)
{
	
	self->currentIteratorPair += 1;
	
	//Check for exceeding the bounds of the queue
	//and enlarge it if necessary
	if(self->currentIteratorPair >= self->iteratorQueueSizeInPairs)
	{
		const int NEW_SIZE = self->iteratorQueueSizeInPairs*2*2;
		const int NEW_SIZE_BYTES = sizeof(PyObject*)*NEW_SIZE;
		PyObject ** const replacement = PyMem_Realloc(self->iteratorQueue,NEW_SIZE_BYTES);
		
		if(not replacement)
		{
			PyErr_NoMemory();
			return false;
		}
		
		self->iteratorQueue = replacement;
		self->iteratorQueueSizeInPairs = NEW_SIZE;
	}
	
	//Retrieve the generator constructor
	PyObject * const generatorObject = PyObject_GetAttrString(multipartModule,"Generator");
	
	if(not generatorObject)
	{
		PyErr_SetString(PyExc_NameError,"Cannot find multipart.Generator");
		return false;
	}
	
	//Retrieve this objects own read method
	PyObject * const read = PyObject_GetAttrString((PyObject*)self,"read");
	
	if(not read)
	{
		PyErr_SetString(PyExc_NameError,"Could not find self.read");
		return false;
	}
	
	//Construct two iterators, both being passed the read method
	//of this object
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
	
	//These iterators are placed into the queue. They are now
	//the current set of iterators into which the parser pushes
	//data.
	self->iteratorQueue[self->currentIteratorPair*2] = headerIterator;
	self->iteratorQueue[self->currentIteratorPair*2+1] = bodyIterator;
	
	return true;
}
  
static int multipart_Parser_on_header_field(void * actor, const char * data, size_t length)
{
	
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

	
	multipart_Parser * const self = actor;
	
	//Append this to the existing buffer, enlarging the buffer 
	//if need be
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
	
	
	multipart_Parser * const self = actor;
	
	PyObject * const bytes = PyString_FromStringAndSize(data,(Py_ssize_t)length);
	
	if(not bytes)
	{
		PyErr_NoMemory();
		return 1;
	}
	
	//Pack into a tuple
	PyObject * const tuple = PyTuple_Pack(1,bytes);
	Py_DECREF(bytes);
	
	if(not tuple)
	{
		PyErr_NoMemory();
		return 1;
	}
	
	//Get the push method of the generator which is the current destination
	//for headers
	PyObject * const push = PyObject_GetAttrString(self->iteratorQueue[self->currentIteratorPair*2+1],"push");
	
	if(not push)
	{
		PyErr_SetString(PyExc_NameError,"Cannot find Generator.push");
		Py_DECREF(tuple);
		return 1;
	}
	
	//Pass the tuple object to the generator
	PyObject * const result = PyObject_Call(push,tuple,NULL);
	Py_DECREF(tuple);
	Py_DECREF(push);
	
	if(not result)
	{
		return 1;
	}
	
	Py_DECREF(result);
	
	return 0;
}

static int multipart_Parser_on_header_value_end(void * actor)
{
	
	multipart_Parser * const self = actor;
	
	//Null terminate both buffers
	self->headerFieldInProgress[self->headerFieldLength] = '\0';
	self->headerValueInProgress[self->headerValueLength] = '\0';
	
	//Construct two string objects, one for the field and one
	//for the value
	PyObject * const field = PyString_FromString(self->headerFieldInProgress);
	PyObject * const value = PyString_FromString(self->headerValueInProgress);
	
	if(not value or not field)
	{
		Py_XDECREF(field);
		Py_XDECREF(value);
		PyErr_NoMemory();
		return 1;
	}
	
	//Pack both into a tuple that will have the form of 
	// ( Name, Value)
	PyObject * const tuple = PyTuple_Pack(2,field,value);
	Py_DECREF(field);
	Py_DECREF(value);
	
	if(not tuple)
	{
		PyErr_NoMemory();
		return 1;
	}
	
	//Get the push method of the generator which is the current destination
	//for headers
	PyObject * const push = PyObject_GetAttrString(self->iteratorQueue[self->currentIteratorPair*2],"push");
	
	if(not push)
	{
		PyErr_SetString(PyExc_NameError,"Cannot find Generator.push");
		Py_DECREF(tuple);
		return 1;
	}
	
	//Pass the tuple object to the generator
	PyObject * const result = PyObject_Call(push,tuple,NULL);
	Py_DECREF(tuple);
	Py_DECREF(push);
	
	if(not result)
	{
		return 1;
	}
	
	Py_DECREF(result);
	
	//This header is now complete. The length of the buffers is now
	//zero'd.
	self->headerValueLength = 0;
	self->headerFieldLength = 0;
	return 0;
}

static int multipart_Parser_on_headers_complete(void * actor)
{
	
	multipart_Parser * const self = actor;
	self->headersComplete = true;
	
	//Get the done method from the current header generator
	PyObject * const done = PyObject_GetAttrString(self->iteratorQueue[self->currentIteratorPair*2],"done");
	
	if(not done)
	{
		PyErr_SetString(PyExc_NameError,"Cannot find Generator.done");
		return 1;
	}
	
	//Signal to the header generator that no more 
	//headers are coming
	PyObject * const emptyTuple = PyTuple_Pack(0);
	
	if(not emptyTuple)
	{
		PyErr_NoMemory();
		return 1;
	}
	
	PyObject * const result = PyObject_Call(done,emptyTuple,NULL);
	Py_DECREF(done);
	Py_DECREF(emptyTuple);
	
	if(not result)
	{
		return 1;
	}
	Py_DECREF(result);
	
	return 0;
}

static int multipart_Parser_on_part_data_end(void * actor)
{
	multipart_Parser * const self = actor;

	
	//Get the done method from the current data generator
	PyObject * const done = PyObject_GetAttrString(self->iteratorQueue[self->currentIteratorPair*2+1],"done");
	
	if(not done)
	{
		PyErr_SetString(PyExc_NameError,"Cannot find Generator.done");
		return 1;
	}
	
	//Signal to the header generator that no more 
	//headers are coming
	PyObject * const emptyTuple = PyTuple_Pack(0);
	
	if(not emptyTuple)
	{
		PyErr_NoMemory();
		return 1;
	}
	
	PyObject * const result = PyObject_Call(done,emptyTuple,NULL);
	Py_DECREF(done);
	Py_DECREF(emptyTuple);
	
	if(not result)
	{
		return 1;
	}
	Py_DECREF(result);
	
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
	
	//Extract from the file input argument a method which can be used
	//as an iterator
	self->readIterator = PyObject_GetIter(fin);
	
	if(not self->readIterator) 
	{
		PyErr_SetString(PyExc_AttributeError,"fin must be iterable");
		return -1;
	}
	
	//Construct the parser with the provided boundary
	self->parser = multipart_parser_init(boundary,&callbackRegistry);
	if( not self->parser )
	{
		PyErr_SetString(PyExc_MemoryError,"multipart_parser_init returned NULL");
		return -1;
	}
	
	//Pass the parser a pointer to this object. It passes it back as a
	//the first argument to all the callbacks 
	multipart_parser_set_data(self->parser,(void*)self);
	
	//Build the queue used for the iterators
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
	//Retrieve bytes from the underlying data stream.
	//In this case, an iterator
	PyObject * const i = PyIter_Next(self->readIterator);
	
	//If the iterator returns NULL, then no more data is available.
	if(i == NULL)
	{
		Py_RETURN_NONE;
	}
	
	//Treat the returned object as just bytes
	PyObject * const bytes = PyObject_Bytes(i);
	Py_DECREF(i);
	
	if( not bytes )
	{
		PyErr_SetString(PyExc_ValueError,"iterable must return bytes like objects");
		return NULL;
		
	}
	
	//Extract from the bytes the raw data
	char const * raw;
	Py_ssize_t length;
	
	if(-1==PyString_AsStringAndSize(bytes,&raw,&length))
	{
		PyErr_SetString(PyExc_ValueError,"iterable must return bytes like objects");
		Py_DECREF(bytes);
		return NULL;
	}

	//Pass the raw data to the parser
	const size_t result = multipart_parser_execute(self->parser,raw,length) ;
	//Add the bytes parsed to the count
	self->bytesParsed += result;
	
	//The parser returns the number of bytes parsed. It not all bytes
	//are parsed, then an error occurred.
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
	//If there exists no more data then return immediately
	if(self->dataComplete)
	{
		return NULL;
	}

	//Retrieve this objects read method
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
	
	
	PyObject * const emptyTuple = PyTuple_New(0);
	
	//Check to see if the iterator pair being returned is getting ahead
	//of the iterator pair being populated. This cannot be allowed
	//to happen.
	while(self->outgoingIteratorPair > self->currentIteratorPair)
	{
		
		//If there is no more data, then return immediately
		if(self->dataComplete)
		{
			Py_DECREF(emptyTuple);
			Py_DECREF(read);
			return NULL;
		}

		//Call this objects read method. This does not return a 
		//meaningful value (just None) but does update all of the internal
		//values being checked here.
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
	
	//Build a tuple of the current set of iterators that should be exposed
	//This tuple is of the form
	// (Headers, Data)
	PyObject * retval = PyTuple_Pack(2,self->iteratorQueue[self->outgoingIteratorPair*2],self->iteratorQueue[self->outgoingIteratorPair*2+1]);
	
	if(not retval)
	{
		PyErr_SetString(PyExc_RuntimeError,"Failed to build tuple");
		return NULL;
	}
	
	self->outgoingIteratorPair += 1;
	
	return retval;
}

static PyMethodDef Parser_methods[] = { {"read",(PyCFunction)Parser_read, METH_KEYWORDS, "read from input source"},{NULL,NULL,0,NULL} };
static PyMemberDef Parser_members[] = { {NULL} };

PyTypeObject multipart_ParserType = {
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
