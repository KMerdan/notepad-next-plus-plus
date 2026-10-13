# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise rejected artifacts with real Mach-O load commands and code seals."""
import importlib.util
import json
import platform
import plistlib
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock
from pathlib import Path

REPO=Path(__file__).resolve().parents[2]
def load(name):
    spec=importlib.util.spec_from_file_location(name,REPO/'scripts'/f'{name}.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
verifier=load('verify-macos-bundle')
packager=load('package-macos')

@unittest.skipUnless(platform.system()=='Darwin','Native macOS deployment tools are required')
class BundleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary=tempfile.TemporaryDirectory(prefix='nnp-bundle-contract-')
        cls.root=Path(cls.temporary.name);cls.arch=platform.machine()
        cls.base=cls.root/'Fixture.app';contents=cls.base/'Contents'
        for directory in ['MacOS','Frameworks','Resources','PlugIns/platforms']:(contents/directory).mkdir(parents=True)
        (cls.root/'lib.c').write_text('int fixture(void){return 0;}\n')
        (cls.root/'main.c').write_text('extern int fixture(void); int main(void){return fixture();}\n')
        cls.run_command(['xcrun','clang','-arch',cls.arch,'-mmacosx-version-min=13.5','-dynamiclib',str(cls.root/'lib.c'),'-install_name','@rpath/libfixture.dylib','-o',str(contents/'Frameworks/libfixture.dylib')])
        cls.run_command(['xcrun','clang','-arch',cls.arch,'-mmacosx-version-min=13.5',str(cls.root/'main.c'),'-L'+str(contents/'Frameworks'),'-lfixture','-Wl,-rpath,@executable_path/../Frameworks','-o',str(contents/'MacOS/Fixture')])
        shutil.copyfile(contents/'Frameworks/libfixture.dylib',contents/'PlugIns/platforms/libqcocoa.dylib')
        (contents/'Info.plist').write_bytes(plistlib.dumps({'CFBundleExecutable':'Fixture','CFBundleIdentifier':'org.notepadnext.bundle-fixture','CFBundlePackageType':'APPL','CFBundleVersion':'1.0','CFBundleShortVersionString':'1.0','LSMinimumSystemVersion':'13.5'}))
        (contents/'Resources/qt.conf').write_text('[Paths]\nPlugins=PlugIns\n')
        # Signing the main executable also seals the app. Seal nested code first;
        # Intel linkers do not provide the initial ad-hoc seals present on arm64.
        for binary in verifier.machos(cls.base):
            if binary!=contents/'MacOS/Fixture':
                cls.run_command(['/usr/bin/codesign','--force','--sign','-','--options','runtime',str(binary)])
        profile=cls.root/'development.entitlements';profile.write_bytes(plistlib.dumps({'com.apple.security.cs.disable-library-validation':True}))
        cls.run_command(['/usr/bin/codesign','--force','--sign','-','--options','runtime','--entitlements',str(profile),str(cls.base)])
    @classmethod
    def tearDownClass(cls):cls.temporary.cleanup()
    @staticmethod
    def run_command(args):
        result=subprocess.run(args,capture_output=True,text=True)
        if result.returncode:raise RuntimeError(f'{args[0]} exited {result.returncode}: {result.stderr}')
        return result
    def setUp(self):
        self.case=Path(tempfile.mkdtemp(dir=self.root));self.app=self.case/'Fixture.app';shutil.copytree(self.base,self.app)
    def inspect(self,signatures=False):return verifier.inspect(self.app,self.arch,signatures,require_node=False)
    def test_signed_contained_bundle(self):
        self.assertTrue(self.inspect(True)['signaturesVerified'])
        self.run_command([str(self.app/'Contents/MacOS/Fixture')])
    def test_external_rpath(self):
        self.run_command(['/usr/bin/install_name_tool','-add_rpath','/private/tmp',str(self.app/'Contents/MacOS/Fixture')])
        with self.assertRaisesRegex(ValueError,'External runtime search path'):self.inspect()
    def test_missing_dependency(self):
        (self.app/'Contents/Frameworks/libfixture.dylib').unlink()
        with self.assertRaisesRegex(ValueError,'Unresolved/external dependency'):self.inspect()
    def test_symlink_escape(self):
        (self.app/'Contents/Resources/escape').symlink_to(self.root/'lib.c')
        with self.assertRaisesRegex(ValueError,'Escaping/broken'):self.inspect()
    def test_underdeclared_minimum_os(self):
        path=self.app/'Contents/Info.plist';info=plistlib.loads(path.read_bytes());info['LSMinimumSystemVersion']='12.0';path.write_bytes(plistlib.dumps(info))
        with self.assertRaisesRegex(ValueError,'Bundle declares an OS lower'):self.inspect()
    def test_machine_specific_qt_config(self):
        (self.app/'Contents/Resources/qt.conf').write_text('[Paths]\nPlugins=/opt/other-qt/plugins\n')
        with self.assertRaisesRegex(ValueError,'Machine-specific qt.conf path'):self.inspect()
    def test_escaping_qt_config(self):
        (self.app/'Contents/Resources/qt.conf').write_text('[Paths]\nPrefix=../../../\n')
        with self.assertRaisesRegex(ValueError,'Escaping qt.conf path'):self.inspect()
    def test_tampered_resource_seal(self):
        with (self.app/'Contents/Resources/qt.conf').open('a') as output:output.write('\n; changed after signing\n')
        with self.assertRaises(subprocess.CalledProcessError):self.inspect(True)
    def test_missing_hardened_runtime(self):
        self.run_command(['/usr/bin/codesign','--force','--sign','-',str(self.app)])
        with self.assertRaisesRegex(ValueError,'no hardened runtime'):self.inspect(True)
    def test_debug_entitlement(self):
        profile=self.case/'debug.entitlements';profile.write_bytes(plistlib.dumps({'com.apple.security.get-task-allow':True,'com.apple.security.cs.disable-library-validation':True}))
        self.run_command(['/usr/bin/codesign','--force','--sign','-','--options','runtime','--entitlements',str(profile),str(self.app)])
        with self.assertRaisesRegex(ValueError,'Unsafe main executable entitlements'):self.inspect(True)
    def test_missing_webengine_resources(self):
        engine=self.app/'Contents/Frameworks/QtWebEngineCore.framework';helper=engine/'Versions/A/Helpers/QtWebEngineProcess.app/Contents/MacOS';helper.mkdir(parents=True)
        shutil.copyfile(self.app/'Contents/MacOS/Fixture',helper/'QtWebEngineProcess')
        (engine/'Resources').mkdir()
        with self.assertRaisesRegex(ValueError,'Missing WebEngine resource: icudtl.dat'):self.inspect()
    def test_unverified_node_archive(self):
        archive=self.case/'unverified.tar.xz';archive.write_bytes(b'not an official Node archive')
        with self.assertRaisesRegex(ValueError,'controlled runtime lock'):packager.checked_node(self.app,archive,self.arch)

    def test_rejected_notarization_does_not_staple(self):
        import io
        response=subprocess.CompletedProcess(['fixture'],0,json.dumps({'status':'Invalid','id':'fixture'}),'')
        with mock.patch.object(packager.subprocess,'run',return_value=response) as command:
            with self.assertRaisesRegex(ValueError,'notarization was not accepted'):packager.notarize(self.app,'fixture-profile',io.StringIO())
            self.assertEqual(command.call_count,1)
    def test_disk_image_signature_is_verified(self):
        image=self.case/'signed.dmg'
        subprocess.run(['/usr/bin/hdiutil','create','-volname','Bundle Signature Fixture','-srcfolder',str(self.app),'-format','UDZO',str(image)],check=True,capture_output=True)
        with (self.case/'dmg-signing.log').open('w') as log:packager.sign_disk_image(image,'-',log)
        subprocess.run(['/usr/bin/codesign','--verify','--strict',str(image)],check=True,capture_output=True)

if __name__=='__main__':unittest.main(verbosity=2)
