import multipart
import unittest

class TestMultipart(unittest.TestCase):
	def setUp(self):
		pass
		
	def test_create(self):
		multipart.Parser('x',[])
		
	def test_0(self):
		for headers,body in  multipart.Parser('------------------------------75766a6a01a6',open('tests/test0.txt')):
			print 'begin headers'
			for header in headers:
				print 'HEADER=' + str(header)
			print 'end headers'
		
	def test_1(self):
		return
		def wrapper(i):
				for j in i:
					for k in j:
						yield k
		for headers,body in multipart.Parser('------------------------------75766a6a01a6',wrapper(open('tests/test0.txt'))):
			for header in headers:
				print header					
		
if __name__ == '__main__':
    unittest.main()
