import copy
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from packages import manifest, resolve, satisfies, elf_arm64
ROOT=Path(__file__).resolve().parents[2]
class Packages(unittest.TestCase):
    def setUp(self):
        self.base={
            'id':'fixture_one','display_name':'Framework Fixture','author':'BedrockForge tests',
            'version':'0.1.0','framework_api':'1.0.0','minecraft_versions':['1.26.30.5'],
            'dependencies':{},'optional_dependencies':{},'capabilities':['core.items.v1'],
            'native':{'library':'libfixture_one.so','entry_point':'bf_mod_entry','abi':'arm64-v8a','mod_api':2}
        }
        self.first=copy.deepcopy(self.base)
        self.second=copy.deepcopy(self.base)
        self.second.update(id='fixture_two',display_name='Second Framework Fixture')
        self.second['native']['library']='libfixture_two.so'
    def test_unverified_game_blocked(self):
        manifest(self.first)
        with self.assertRaises(ValueError): resolve([self.first],'1.21.80',self.first['capabilities'])
        self.assertEqual(resolve([self.first],'1.21.80',[],safe=True),[])
    def test_dependencies(self):
        self.second['dependencies']={'fixture_one':'==0.1.0'}
        caps=self.first['capabilities']+self.second['capabilities']
        self.assertEqual([m['id'] for m in resolve([self.second,self.first],'1.26.30.5',caps)],['fixture_one','fixture_two'])
        self.second['dependencies']={'absent':'==1.0.0'}
        with self.assertRaises(ValueError): resolve([self.second],'1.26.30.5',caps)
    def test_invalid(self):
        self.first['native']['library']='../libevil.so'
        with self.assertRaises(ValueError): manifest(self.first)
        self.assertTrue(satisfies('1.2.0','>=1.1.0'))
        self.assertFalse(satisfies('1.0.0','>=1.1.0'))
        with self.assertRaises(ValueError): satisfies('1.0.0','^1.0.0')
    def test_cycles_and_capabilities(self):
        self.first['dependencies']={'fixture_two':'==0.1.0'}
        self.second['dependencies']={'fixture_one':'==0.1.0'}
        caps=self.first['capabilities']+self.second['capabilities']
        with self.assertRaises(ValueError): resolve([self.first,self.second],'1.26.30.5',caps)
        with self.assertRaises(ValueError): resolve([self.first],'1.26.30.5',[])
if __name__=='__main__':unittest.main()
