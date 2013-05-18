import multipart
import unittest

class TestMultipart(unittest.TestCase):
	def setUp(self):
		pass
		
	def test_create(self):
		multipart.Parser('x')
		
	def test_0(self):
		multipart.Parser('------------------------------75766a6a01a6')(open('tests/test0.txt'))
		
if __name__ == '__main__':
    unittest.main()
