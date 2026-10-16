"""Exercise the shared package hook with fake devices, never the host camera."""
from pathlib import Path
import subprocess
import tempfile
import unittest

class CameraHook(unittest.TestCase):
    def check(self, name=None, loaded=False, fail=False):
        with tempfile.TemporaryDirectory(prefix='callayers-hook-') as directory:
            root = Path(directory)
            (root/'dev').mkdir()
            (root/'sys/class/video4linux/video42').mkdir(parents=True)
            if name is not None:
                (root/'dev/video42').touch()
                (root/'sys/class/video4linux/video42/name').write_text(name+'\n')
            source = Path(__file__).resolve().parents[1]/'linux/packaging/setup-camera.sh'
            script = source.read_text().replace('/dev/video42',str(root/'dev/video42')).replace('/sys/class/video4linux/video42',str(root/'sys/class/video4linux/video42'))
            (root/'hook.sh').write_text(script)
            (root/'bin').mkdir()
            (root/'bin/lsmod').write_text('#!/bin/sh\necho "'+('v4l2loopback 1024 0' if loaded else '')+'"\n')
            (root/'bin/modprobe').write_text('#!/bin/sh\necho invoked > "'+str(root/'called')+'"\n'+('exit 1\n' if fail else f'touch "{root}/dev/video42"\necho Callayers > "{root}/sys/class/video4linux/video42/name"\n'))
            for command in (root/'bin').iterdir(): command.chmod(0o755)
            result = subprocess.run(['bash',str(root/'hook.sh')],env={'PATH':f'{root}/bin:/usr/bin:/bin'},text=True,capture_output=True)
            return result,(root/'called').exists()

    def test_creates_own_camera_once(self):
        result,called = self.check()
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertTrue(called)
        result,called = self.check('Callayers',True)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertFalse(called)

    def test_never_takes_over_another_camera_or_loaded_setup(self):
        for name,loaded in [('Other camera',True),(None,True)]:
            result,called=self.check(name,loaded)
            self.assertNotEqual(result.returncode,0)
            self.assertFalse(called)

    def test_driver_load_failure_is_reported(self):
        result,called=self.check(fail=True)
        self.assertNotEqual(result.returncode,0)
        self.assertTrue(called)

if __name__=='__main__': unittest.main()
