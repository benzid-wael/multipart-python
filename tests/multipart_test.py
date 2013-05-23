import multipart
import unittest

class TestMultipart(unittest.TestCase):
	def setUp(self):
		pass
		
	def test_create(self):
		multipart.Parser('x',[])
		
	def test_0(self):
		for headers,data in  multipart.Parser('------------------------------75766a6a01a6',open('tests/test0.txt')):
			print 'begin headers'
			for header in headers:
				print 'HEADER=' + str(header)
			print 'end headers'
			for d in data:
				print 'LEN=' + str(len(d))
		
	def test_1(self):
		def wrapper(i):
				for j in i:
					for k in j:
						yield k
		for headers,data in multipart.Parser('------------------------------75766a6a01a6',wrapper(open('tests/test0.txt'))):
			for header in headers:
				print 'HEADER=' + str(header)
			print 'end headers'
			for d in data:
				print 'LEN=' + str(len(d))			
		
if __name__ == '__main__':
    unittest.main()
