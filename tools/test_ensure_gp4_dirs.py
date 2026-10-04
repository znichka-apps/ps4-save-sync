"""Exercise missing GP4 directories, metadata preservation, and idempotence."""
from pathlib import Path
import tempfile
import unittest
from xml.dom import minidom

from ensure_gp4_dirs import children, ensure_directories


GP4 = '''<?xml version="1.0"?>
<psproject xmlns:xsd="http://www.w3.org/2001/XMLSchema"
           xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" fmt="gp4" version="1000">
  <!-- preserve package metadata and file attributes -->
  <volume>
    <volume_type>pkg_ps4_app</volume_type><volume_id>PS4VOLUME</volume_id>
    <volume_ts>2026-10-04 12:00:00</volume_ts>
    <package content_id="IV0000-PSSY00001_00-PS4SAVESYNC00000"
             passcode="00000000000000000000000000000000" storage_type="digital50" app_type="full"/>
    <chunk_info chunk_count="1" scenario_count="1">
      <chunks><chunk id="0" layer_no="0" label="Chunk #0"/></chunks>
      <scenarios default_id="0"><scenario id="0" type="sp" initial_chunk_count="1">0</scenario></scenarios>
    </chunk_info>
  </volume>
  <files img_no="0">
    <file targ_path="eboot.bin" orig_path="eboot.bin"/>
    <file targ_path="assets/google/cacert.pem" orig_path="assets/google/cacert.pem" pfs_compression="enable"/>
    <file targ_path="assets/google/licenses/nested/license.txt" orig_path="external folder/license.txt"/>
    <file targ_path="assets/images/nested/icon.png" orig_path="images/icon.png"/>
    <file targ_path="assets/google/README.txt" orig_path="assets/google/README.txt"/>
    <file targ_path="sce_sys/about/right.sprx" orig_path="sce_sys/about/right.sprx"/>
    <file targ_path="sce_module/libc.prx" orig_path="sce_module/libc.prx"/>
    <file targ_path="extra/a&amp;b/data.dat" orig_path="external/a&amp;b.dat"/>
  </files>
  <rootdir><dir targ_name="assets" custom="preserve"><dir targ_name="images"><dir targ_name="nested"/></dir></dir>
    <dir targ_name="sce_sys"><dir targ_name="about"/></dir><dir targ_name="unused"/></rootdir>
  <custom_metadata value="unchanged">custom text</custom_metadata>
</psproject>'''


class DirectoryTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.path = Path(self.temporary.name) / 'sample.gp4'

    def write(self, text):
        self.path.write_text(text, encoding='utf-8')

    def assert_complete(self):
        with minidom.parse(str(self.path)) as doc:
            project = doc.documentElement
            root = children(project, 'rootdir')[0]
            for file in children(children(project, 'files')[0], 'file'):
                parent = root
                for name in file.getAttribute('targ_path').split('/')[:-1]:
                    matches = [d for d in children(parent, 'dir') if d.getAttribute('targ_name') == name]
                    self.assertEqual(len(matches), 1)
                    parent = matches[0]

    def test_nested_directories_and_metadata(self):
        self.write(GP4)
        with minidom.parseString(GP4) as before:
            metadata = [n.toxml() for n in before.documentElement.childNodes
                        if n.nodeType == n.ELEMENT_NODE and n.tagName != 'rootdir']
            attributes = dict(before.documentElement.attributes.items())
        self.assertEqual(ensure_directories(self.path), 6)
        self.assert_complete()
        with minidom.parse(str(self.path)) as after:
            self.assertEqual(dict(after.documentElement.attributes.items()), attributes)
            self.assertEqual([n.toxml() for n in after.documentElement.childNodes
                              if n.nodeType == n.ELEMENT_NODE and n.tagName != 'rootdir'], metadata)
            root = children(after.documentElement, 'rootdir')[0]
            assets = next(d for d in children(root, 'dir') if d.getAttribute('targ_name') == 'assets')
            self.assertEqual(assets.getAttribute('custom'), 'preserve')
            self.assertTrue(any(d.getAttribute('targ_name') == 'unused' for d in children(root, 'dir')))
            self.assertIn('preserve package metadata', after.toxml())
        repaired = self.path.read_bytes()
        self.assertEqual(ensure_directories(self.path), 0)
        self.assertEqual(self.path.read_bytes(), repaired)

    def test_missing_or_empty_rootdir(self):
        for root in ('', '<rootdir/>'):
            with self.subTest(root=root):
                self.write('<psproject><files><file targ_path="assets/google/cacert.pem"/></files>' + root + '</psproject>')
                self.assertEqual(ensure_directories(self.path), 2)
                self.assert_complete()

    def test_root_file_needs_no_directory(self):
        self.write('<psproject><files><file targ_path="eboot.bin"/></files><rootdir/></psproject>')
        original = self.path.read_bytes()
        self.assertEqual(ensure_directories(self.path), 0)
        self.assertEqual(self.path.read_bytes(), original)

    def test_bad_paths_leave_input_intact(self):
        for target in ('', '../data', '/data', 'assets//data', 'assets/./data', 'assets\\data'):
            with self.subTest(target=target):
                self.write('<psproject><files><file targ_path="' + target + '"/></files><rootdir/></psproject>')
                original = self.path.read_bytes()
                with self.assertRaises(ValueError):
                    ensure_directories(self.path)
                self.assertEqual(self.path.read_bytes(), original)


if __name__ == '__main__':
    unittest.main()
