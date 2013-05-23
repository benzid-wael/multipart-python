#include "multipart_Generator.h"
#include "stdbool.h"
#include "iso646.h"

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



PyTypeObject multipart_GeneratorType = {
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


