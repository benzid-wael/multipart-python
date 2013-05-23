import multipart

from wsgiref.simple_server import make_server

filenumber = 0

def application(env,start_response):
	
	if env['REQUEST_METHOD'] != 'POST':
		msg = '405 Method Not Allowed'
		response_headers = [('Content-Type', 'text/plain'),
		('Content-Length', str(len(msg)))]

		start_response(msg,response_headers);
		return msg

	#The Content type header contains the boundary definition
	#which must be passed to the parser
	contentType = env['CONTENT_TYPE']
	
	magic = 'boundary='
	offset = contentType.index(magic)
	
	#The RFC states that the boundary used includes two more dashes 
	#than what is passed in the header
	boundary = '--' + contentType[offset + len(magic):]
	
	for headers, data in multipart.Parser(boundary,env['wsgi.input']):
		#headers is an iterator returning tuples of the form
		# (name, value)
		
		#data is an iterator return chunks of data from this part
		
		global filenumber
		filename = 'savedfile_%i' % filenumber
		filenumber += 1
		with open(filename,'w') as fout:
			for chunk in data:
				fout.write(chunk)
				
		print 'saved file:%s' % filename
				
	
	status = '200 OK'
	msg = 'Uploaded ok!'
	response_headers = [('Content-Type', 'text/plain'),
	('Content-Length', str(len(msg)))]
	
	start_response(status,response_headers)
	
	return msg

httpd = make_server('localhost',8080,application)
httpd.serve_forever()

