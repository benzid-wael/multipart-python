import multipart
import unittest

class TestMultipart(unittest.TestCase):
	def setUp(self):
		pass
		
	def test_create(self):
		multipart.Parser('x',[])
		
	def test_0(self):
		for part in  multipart.Parser('------------------------------75766a6a01a6',open('tests/test0.txt')):
			print part
		
	def test_1(self):
		def wrapper(i):
				for j in i:
					for k in j:
						yield k
		for part in multipart.Parser('------------------------------75766a6a01a6',wrapper(open('tests/test0.txt'))):
			print part
					
		
if __name__ == '__main__':
    unittest.main()
