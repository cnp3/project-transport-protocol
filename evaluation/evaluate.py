import functools
import sys
import os
import signal
import logging
import datetime
import subprocess
import collections
import abc
import json
import hashlib
import tempfile
import time


REF_SENDER = '/path/to/the/sender'
REF_RECEIVER = '/path/to/the/receiver'
LINKSIM = '/path/to/the/link_sim'
LINKSIMKEYS = {'port': 'p',
               'forward_port': 'P',
               'delay': 'd',
               'jitter': 'j',
               'err_rate': 'e',
               'cut_rate': 'c',
               'loss_rate': 'l',
               'seed': 's'}

pjoin = os.path.join

BDIR = '/path/to/store/log_files'
LOGDIR = '%s/testslogs' % BDIR
if not os.path.exists(LOGDIR):
    os.mkdir(LOGDIR)
logging.basicConfig()
log = logging.getLogger()
log.addHandler(logging.FileHandler(pjoin(LOGDIR, 'log.log'), mode='w'))
log.setLevel(logging.INFO)
RESULTS = '%s_%s.json' % ('results', datetime.datetime.now().isoformat())
MAKEFILE = 'Makefile'
SENDER = 'sender'
RECEIVER = 'receiver'
OK = 'OK'
RDICT = collections.defaultdict(dict)
ARCHIVE = 'exp.zip'
MAX_PKT_SIZE = 528
MAX_LOG_SIZE = 1000000


def sync():
    subprocess.call(['sync'])


def terminate(*args):
    for p in args:
        try:
            os.killpg(os.getpgid(p.pid), signal.SIGTERM)
        except:
            pass


def fclose(*args):
    for f in args:
        try:
            f.flush()
            f.close()
            sync()
        except:
            pass


class FailedTest(Exception):
    def __init__(self, cause='', *args, **kwargs):
        super(FailedTest, self).__init__(*args, **kwargs)
        self.cause = cause


def change_write(f, limit):
    f.orig_write = f.write
    def new_write(s):
        if f.tell() < limit:
            size = min(len(s), limit - f.tell())
            f.orig_write (s[:size])

    f.write = new_write


class Test(object):
    def __init__(self, name, submission, working_dir, *args, **kwargs):
        super(Test, self).__init__(*args, **kwargs)
        self.name = name
        self.submission = submission
        self.logdir = os.path.join(LOGDIR, name.replace(' ', '_'))
        self.working_dir = working_dir
        self.logbase = pjoin(self.logdir, self.submission)
        if not os.path.exists(self.logdir):
            os.mkdir(self.logdir)

    def log(self, msg, *args):
        exp_msg = '[%s/%s] %s' % (self.name, self.submission, msg)
        log.info(exp_msg, *args)

    def popen(self, *args, **kwargs):
        taskname = args[0][0].split('/')[-1]
        err = open('%s.%s.err' % (self.logbase, taskname), 'w')
        out = open('%s.%s.out' % (self.logbase, taskname), 'w')

        change_write(err, MAX_LOG_SIZE)
        change_write(out, MAX_LOG_SIZE)

        stdin = kwargs.pop('stdin', None)
        self.log('popen: %s, %s from: %s', args, kwargs, self.working_dir)
        p = subprocess.Popen(*args, stdin=stdin, stdout=out, stderr=err,
                             cwd=self.working_dir, preexec_fn=os.setsid,
                             close_fds=True, **kwargs)
        return p, out, err

    def call(self, *args, timeout=None, **kwargs):
        p, out, error = self.popen(*args, **kwargs)
        try:
            p.wait(timeout)
        except subprocess.TimeoutExpired:
            if out.tell() > MAX_LOG_SIZE:
                out.truncate(MAX_LOG_SIZE)
            if error.tell() > MAX_LOG_SIZE:
                error.truncate(MAX_LOG_SIZE)
            fclose(out, error)
            os.killpg(os.getpgid(p.pid), signal.SIGTERM)
            raise FailedTest("The test timed out!")
        else:
            if out.tell() > MAX_LOG_SIZE:
                out.truncate(MAX_LOG_SIZE)
            if error.tell() > MAX_LOG_SIZE:
                error.truncate(MAX_LOG_SIZE)
            fclose(out, error)
            return p.returncode

    def run(self, skippable=True, check=False):
        subprocess.call(['killall', '-9', 'link_sim'])
        subprocess.call(['killall', '-9', 'sender'])
        subprocess.call(['killall', '-9', 'receiver'])
        if skippable and self.submission in RDICT\
           and self.name in RDICT[self.submission]\
           and ((check and RDICT[self.submission][self.name]) or
                RDICT[self.submission][self.name] == OK):
            self.log('Found in previous run: %s',
                     RDICT[self.submission][self.name])
            return RDICT[self.submission][self.name] == OK
        success = True
        try:
            rval = self.exec_tests()
        except Exception as e:
            success = False
            try:
                rval = e.cause
            except:
                rval = str(e)
            self.log('Test failed: %s', rval)
        finally:
            self.cleanup()
        self.log('%s', rval)
        RDICT[self.submission][self.name] = rval
        return success

    @abc.abstractmethod
    def exec_tests(self):
        """Execute the test and return a result (bool, str, ...)"""

    def path(self, x):
        return pjoin(self.working_dir, x)

    def cleanup(self):
        pass

###############################################################################
# Sanity tests
###############################################################################


class ArchiveStruct(Test):
    """Checks that the project archive follows the required structure"""

    def __init__(self, basepath, *args, **kwargs):
        super(ArchiveStruct, self).__init__(name='Archive structure',
                                            *args, **kwargs)
        self.basepath = basepath

    def run(self):
        return super(ArchiveStruct, self).run(skippable=False)

    def exec_tests(self):
        for dirpath, _, files in os.walk(self.basepath):
            if MAKEFILE in files:
                self.working_dir = dirpath
                break
        if not self.working_dir:
            raise FailedTest('%s not found!' % MAKEFILE)
        self.log('Makefile found')
        self.check_path('src')
        self.check_path('tests')
        self.check_path('rapport.pdf')
        return OK

    def check_path(self, p):
        if not os.path.exists(self.path(p)):
            raise FailedTest('Missing archive element: %s' % p)
        self.log('%s found' % p)


class Make(Test):
    """Checks that the project compiles by running 'make' in the first
    top-level folder that contains a Makefile"""
    CMD = 'make'

    def __init__(self, *args, **kwargs):
        super(Make, self).__init__(name=Make.CMD, *args, **kwargs)

    def run(self):
        return super(Make, self).run(skippable=False)

    def exec_tests(self):
        self.clean_exec(SENDER)
        self.clean_exec(RECEIVER)
        self.call([Make.CMD], timeout=60)
        self.log('make finished')
        self.check_exec(SENDER)
        self.check_exec(RECEIVER)
        return OK

    def clean_exec(self, x):
        exec_path = self.path(x)
        log.info('Looking for exec %s', exec_path)
        if os.path.exists(exec_path):
            if os.path.isfile(exec_path):
                os.unlink(exec_path)
                sync()
            else:
                raise FailedTest('%s is already present in the archive and is'
                                 ' not a file that can be removed!' % x)

    def check_exec(self, exec_name):
        exec_path = self.path(exec_name)
        if not os.path.exists(exec_path):
            raise FailedTest("Couldn't find executable %s after running make"
                             % exec_name)
        if not os.access(exec_path, os.X_OK):
            raise FailedTest("%s is not executable after running make"
                             % exec_name)
        self.log('Have exec %s' % exec_name)


class MakeTests(Test):
    """Checks that the project Makefile has a tests target and that these
    succeed"""

    def __init__(self, *args, **kwargs):
        super(MakeTests, self).__init__(name='make tests', *args, **kwargs)

    def exec_tests(self):
        self.log('Running make tests with a 5 mins timeout')
        rval = self.call(['make', 'tests'], timeout=5*60)
        if int(rval) != 0:
            raise FailedTest('make tests returned an error code: %s' % rval)
        return OK


###############################################################################
# Is it working ?
###############################################################################

__files = {}  # Cache all created temp files
__textfiles = {}


def get_random_file(size, src='/dev/urandom'):
    try:
        p = __files[size]
        if not os.path.exists(p):
            del __files[size]
            return get_random_file(size, src)
        return p
    except KeyError:
        name = tempfile.mktemp(dir=BDIR)
        subprocess.call(['dd', 'if=%s' % src, 'of=%s' % name,
                         'bs=1', 'count=%s' % size])
        __files[size] = name
        return name


def get_random_textfile(size):
    try:
        p = __textfiles[size]
        if not os.path.exists(p):
            del __textfiles[size]
            return get_random_textfile(size)
        return p
    except KeyError:
        name = tempfile.mktemp(dir=BDIR)
        f = open(name, 'w')
        subprocess.Popen("info bash -o -|shuf |sed 's/  */ /g;s/^ //'|fmt -w 90 | head -c %d" % size, stdout=f, shell=True)
        f.close()
        __textfiles[size] = name
        return name


def hashfile(path):
    bs = 65536
    f = open(path, 'rb')
    hasher = hashlib.sha256()
    buf = f.read(bs)
    while len(buf) > 0:
        hasher.update(buf)
        buf = f.read(bs)
    f.close()
    return hasher.digest()


class BaseTransfer(Test):
    def __init__(self, timeout, *args, **kwargs):
        super(BaseTransfer, self).__init__(*args, **kwargs)
        self.timeout = timeout
        self.lout = self.lerr = None
        self.linksim_args = {
            'port': 1234,
            'forward_port': 12345,
            'delay': 0,
            'jitter': 0,
            'cut_rate': 0,
            'err_rate': 0,
            'loss_rate': 0,
            'seed': 379097246178890316
        }

    def exec_tests(self):
        self.log('Starting transfer test with a timeout of %s' % self.timeout)
        self.l = self.spawn_linksim()
        time.sleep(.1)
        self.r = self.spawn_receiver()
        time.sleep(.1)
        self.s = self.spawn_sender()
        time.sleep(.1)
        for i in range(0, int(self.timeout*10)):
            if self.s.poll() is not None or self.r.poll() is not None\
                    or self.s.returncode is not None\
                    or self.r.returncode is not None:
                self.log("One of the processes has ended")
                break
            time.sleep(.1)
        log.info('Stopping experiment')
        try:
            if not self.s.returncode:
                self.s.wait(1)
        except subprocess.TimeoutExpired:
            terminate(self.s)
        self.log("Sender is stopped (rcode: %s)", self.s.returncode)
        try:
            if not self.r.returncode:
                self.r.wait(5)
        except subprocess.TimeoutExpired:
            terminate(self.r)
        self.log("Receiver is stopped (rcode: %s)", self.r.returncode)
        terminate(self.l)
        self.log("Checking transfert result")
        time.sleep(1)
        sync()
        self.check_end_transfer()
        return OK

    @abc.abstractmethod
    def check_end_transfer(self):
        """Check that the transfer was successful"""

    @abc.abstractmethod
    def spawn_sender(self):
        """Initialize and start an instance of the sender program"""

    @abc.abstractmethod
    def spawn_receiver(self):
        """Initialize and start an instance of the receiver program"""

    def spawn_linksim(self):
        args = [LINKSIM, '-R']
        for k, v in self.linksim_args.items():
            args.append('-%s' % LINKSIMKEYS[k])
            args.append(str(v))
        linksim, out, err = self.popen(args)
        self.lout, self.lerr = out, err
        return linksim

    def cleanup(self):
        if self.lout.tell() > MAX_LOG_SIZE:
            self.lout.truncate(MAX_LOG_SIZE)
        if self.lerr.tell() > MAX_LOG_SIZE:
            self.lerr.truncate(MAX_LOG_SIZE)
        fclose(self.lout, self.lerr)
        terminate(self.l, self.r, self.s)
        super(BaseTransfer, self).cleanup()


class FileTransfer(BaseTransfer):
    def __init__(self, sender_port=1234, receiver_port=12345,
                 sender_use_stdin=False, receiver_use_stdout=False,
                 sender_host='::1', receiver_host='::',
                 *args, **kwargs):
        super(FileTransfer, self).__init__(*args, **kwargs)
        self.sport = str(sender_port)
        self.rport = str(receiver_port)
        self.shost = sender_host
        self.rhost = receiver_host
        self.sender_use_stdin = sender_use_stdin
        self.receiver_use_stdout = receiver_use_stdout
        # File handles
        self.s_in = self.s_out = self.s_err = self.r_out = self.r_err = None

    @abc.abstractmethod
    def get_file(self):
        """Return the path to the file to use in this transfer."""

    def _spawn(self, name, host, port, use_stdin=False, filename=''):
        args = [name]
        kwargs = {}
        if use_stdin:
            self.s_in = kwargs['stdin'] = open(self.get_file(), 'r')
        if filename and not use_stdin:
            args.append('-f')
            args.append(filename)
        args.append(host)
        args.append(port)
        return self.popen(args, **kwargs)

    def _out_file(self):
        return '%s_out' % self.get_file() if not self.receiver_use_stdout else\
            (self.r_out.name if self.r_out else '')

    def spawn_sender(self):
        sender, out, err = self._spawn(self.get_sender(),
                                       self.shost, self.sport,
                                       use_stdin=self.sender_use_stdin,
                                       filename=self.get_file())
        self.s_out, self.s_err = out, err
        return sender

    def get_sender(self):  # noqa
        return './%s' % SENDER

    def spawn_receiver(self):
        if not self.receiver_use_stdout and os.path.exists(self._out_file()):
            os.unlink(self._out_file())
            sync()
        subprocess.call(['touch', self._out_file()])
        receiver, out, err = self._spawn(self.get_receiver(),
                                         self.rhost, self.rport,
                                         filename=self._out_file())
        self.r_out, self.r_err = out, err
        return receiver

    def get_receiver(self):  # noqa
        return './%s' % RECEIVER

    def check_end_transfer(self):
        fclose(self.s_in, self.s_out, self.r_out, self.r_err, self.s_err)
        sync()
        try:
            h1, h2 = hashfile(self.get_file()), hashfile(self._out_file())
        except FileNotFoundError:
            h1 = 'No output file'
            h2 = ''
        if h1 != h2:
            self.log('Hashes differ: %s - %s', h1, h2)
            s1, s2 = (os.stat(self.get_file()).st_size,
                      os.stat(self._out_file()).st_size)
            self.log('sizes: %d vs %d' % (s1, s2))
            raise FailedTest('The file has been corrupted')


class SimpleTransfer(FileTransfer):
    """Simple transfer without losses or delay"""
    def __init__(self, byte_count, *args, **kwargs):
        name = '%s transfer %sb' % (kwargs.get('name', ''), byte_count)
        kwargs['name'] = name
        if 'timeout' not in kwargs:
            timeout = min(max(byte_count/MAX_PKT_SIZE, 10), 500)
        else:
            timeout = kwargs.pop('timeout')
        super(SimpleTransfer, self).__init__(timeout=timeout,
                                             *args, **kwargs)
        self.f = self.gen_file(byte_count)
        if os.path.exists(self._out_file()):
            os.unlink(self._out_file())
            sync()

    def get_file(self):
        return self.f

    def gen_file(self, byte_count):
        return get_random_file(byte_count)


class Latency(SimpleTransfer):
    def __init__(self, byte_count=150*MAX_PKT_SIZE, ms=50, jitter=30, *args, **kwargs):
        name = '%s latency %s %s' % (kwargs.get('name', ''), ms, jitter)
        kwargs['name'] = name
        super(Latency, self).__init__(byte_count=byte_count,
                                      timeout=byte_count*1.5/MAX_PKT_SIZE,
                                      *args, **kwargs)
        self.linksim_args['delay'] = ms
        self.linksim_args['jitter'] = jitter

###############################################################################
# Is it reliable ?
###############################################################################


class Corruption(SimpleTransfer):
    def __init__(self, err_rate=5, *args, **kwargs):
        name = '%s err_rate %s' % (kwargs.get('name', ''), err_rate)
        kwargs['name'] = name
        if 'byte_count' not in kwargs:
            kwargs['byte_count'] = 150 * MAX_PKT_SIZE
        super(Corruption, self).__init__(*args, **kwargs)
        self.linksim_args['err_rate'] = err_rate


class Truncation(SimpleTransfer):
    def __init__(self, cut_rate=5, *args, **kwargs):
        name = '%s cut_rate %s' % (kwargs.get('name', ''), cut_rate)
        kwargs['name'] = name
        if 'byte_count' not in kwargs:
            kwargs['byte_count'] = 150 * MAX_PKT_SIZE
        super(Truncation, self).__init__(*args, **kwargs)
        self.linksim_args['cut_rate'] = cut_rate


class Loss(SimpleTransfer):
    def __init__(self, loss_rate=5, *args, **kwargs):
        name = '%s loss_rate %s' % (kwargs.get('name', ''), loss_rate)
        kwargs['name'] = name
        if 'byte_count' not in kwargs:
            kwargs['byte_count'] = 150 * MAX_PKT_SIZE
        super(Loss, self).__init__(*args, **kwargs)
        self.linksim_args['loss_rate'] = loss_rate


class Unreliable(Latency, Truncation, Corruption, Loss):
    def __init__(self, *args, **kwargs):
        super(Unreliable, self).__init__(*args, **kwargs)

###############################################################################
# Is it inter-operable ?
###############################################################################


def get_ref_sender(self):
    return REF_SENDER


def get_ref_receiver(self):
    return REF_RECEIVER


def test_ref_sender(TestClass, *args, **kwargs):
    kwargs['name'] = 'refsender_%s' % kwargs.get('name', '')
    t = TestClass(*args, **kwargs)
    t.get_sender = functools.partial(get_ref_sender, t)
    return t


def test_ref_receiver(TestClass, *args, **kwargs):
    kwargs['name'] = 'refreceiver_%s' % kwargs.get('name', '')
    t = TestClass(*args, **kwargs)
    t.get_receiver = functools.partial(get_ref_receiver, t)
    return t


class WindowTest(Latency):
    def __init__(self, win=3, *args, **kwargs):
        byte_count = win * 40 * 520 if 'byte_count' not in kwargs \
            else kwargs['byte_count']
        kwargs['byte_count'] = byte_count
        super(WindowTest, self).__init__(name="limited window",
                                         *args, **kwargs)
        self.win = win

    get_receiver = get_ref_receiver

    def spawn_receiver(self):
        if not self.receiver_use_stdout and os.path.exists(self._out_file()):
            os.unlink(self._out_file())
            sync()
        receiver, out, err = self.popen([self.get_receiver(),
                                         '-f', self._out_file(),
                                         '-b', str(self.win),
                                         self.rhost, self.rport])
        self.r_out, self.r_err = out, err
        return receiver

    def check_end_transfer(self):
        super(WindowTest, self).check_end_transfer()
        window_error = 0
        with open(self.r_out.name, 'r') as f:
            for line in f:
                if "Dropping out of window packet" in line:
                    window_error += 1
        if window_error > 1:
                raise FailedTest('The program did not respect the advertized '
                                 'window (%s)' % window_error)


###############################################################################
# Is it handling corner cases ?
###############################################################################


class Timestamp(SimpleTransfer):
    """This test checks that the receiver properly handles the timestamp field
    """
    def __init__(self, *args, **kwargs):
        super(Timestamp, self).__init__(name="timestamp check",
                                        byte_count=536,
                                        *args, **kwargs)

    def check_end_transfer(self):
        super(Timestamp, self).check_end_transfer()
        with open(self.s_out.name, 'r') as f:
            for l in f:
                if "The receiver is corrupting the timestamp!!!" in l:
                    raise FailedTest('The receiver does not echo timestamp '
                                     'but corrupts them!!!')

    def get_sender(self):
        return REF_SENDER


class Localhost(SimpleTransfer):
    """This test checks that domain names instead of IPv6 addresses can be
    used as hostname parameter"""
    def __init__(self, *args, **kwargs):
        super(Localhost, self).__init__(name='locahost resolution',
                                        byte_count=MAX_PKT_SIZE,
                                        sender_host='localhost',
                                        receiver_host='localhost',
                                        *args, **kwargs)


class SenderSTDIN(SimpleTransfer):
    """This test checks that the sender can read and send data from stdin"""
    def __init__(self, *args, **kwargs):
        super(SenderSTDIN, self).__init__(name='Sender STDIN',
                                          byte_count=1000,
                                          sender_use_stdin=True,
                                          *args, **kwargs)


class ReceiverSTDOUT(SimpleTransfer):
    """This test checks that the receiver can output uncorrupted data to
    stdout"""
    def __init__(self, *args, **kwargs):
        super(ReceiverSTDOUT, self).__init__(name='Received STDOUT',
                                             byte_count=1000,
                                             receiver_use_stdout=True,
                                             *args, **kwargs)


class CharFile(SimpleTransfer):
    """This test checks that sending a file made of ASCII characters works"""
    def __init__(self, *args, **kwargs):
        super(CharFile, self).__init__(name="Char file",
                                       byte_count=8*MAX_PKT_SIZE,
                                       receiver_use_stdout=True,
                                       *args, **kwargs)

    def gen_file(self, byte_count):
        return get_random_textfile(byte_count)


class BlankFile(SimpleTransfer):
    """This test checks that sending a file made of null bytes works"""
    def __init__(self, *args, **kwargs):
        super(BlankFile, self).__init__(name='Blank file',
                                        byte_count=MAX_PKT_SIZE,
                                        receiver_use_stdout=True,
                                        *args, **kwargs)

    def gen_file(self, byte_count):
        return get_random_file(byte_count, src='/dev/zero')


def test_submission(path, name):
    base_args = {'submission': name, 'working_dir': None}
    struct = ArchiveStruct(basepath=pjoin(path, name), **base_args)
    struct.run()
    if not struct.working_dir:
        log.error('Cannot do anything for submission %s!', name)
        return
    base_args['working_dir'] = struct.working_dir
    log.info('Working dir for submission %s is %s', name, struct.working_dir)
    if not Make(**base_args).run():
        log.error('The submission %s does not produces working executables!'
                  % name)
        return
    #MakeTests(**base_args).run()
    #log.info("Re-running make as some groups delete the executables"
    #         "after make tests ...")
    #Make(**base_args).run()
    # Working ?
    i = (0, 1, 4, 520, 536, MAX_PKT_SIZE*256)
    failures = 0
    for b in i:
        if not SimpleTransfer(byte_count=b, **base_args).run(check=True):
            failures += 1
        if failures > len(i)/2:
            log.error('The submission %s cannot transfer data ...' % name)
    #        return
    if not Localhost(**base_args).run(check=True) or \
            not SenderSTDIN(**base_args).run(check=True) or \
            not ReceiverSTDOUT(**base_args).run(check=True):
        log.error('The submission %s does not respect the specifications'
                  % name)
        #return
    # Reliable ?
    i = (Corruption, Loss, Truncation, Latency, Unreliable)
    failures = 0
    for b in i:
        if not b(**base_args).run(check=True):
            failures += 1
        if failures > len(i)/2:
            log.error('The submission %s is not reliable', name)
            break
    # Interop
    failures = 0
    i = (1, 520, MAX_PKT_SIZE*10)
    for b in i:
        if not test_ref_sender(SimpleTransfer,
                               byte_count=b,
                               **base_args).run(check=True):
            failures += 1
        if not test_ref_receiver(SimpleTransfer,
                                 byte_count=b,
                                 **base_args).run(check=True):
            failures += 1
        if failures > (2 * len(i)) / 3:
            log.error('Too many interoperable errors')
    #        return
    i = (Corruption, Loss, Truncation, Latency, Unreliable)
    for c in i:
        test_ref_sender(c, **base_args).run(check=True)
        test_ref_receiver(c, **base_args).run(check=True)
    WindowTest(**base_args).run(check=True)
    CharFile(**base_args).run(check=True)
    BlankFile(**base_args).run(check=True)
    Timestamp(**base_args).run(check=True)


def main(path):
    log.info('---- Session start: %s', datetime.datetime.now().isoformat())
    for submission in os.listdir(path):
        if not os.path.isdir(pjoin(path, submission)):
            log.info('Skipping %s as it is not a directory!', submission)
            continue
        test_submission(path, submission)
        save_results()


def save_results():
    with open(RESULTS, 'w') as f:
        json.dump(RDICT, f)


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print('This script takes the directory containing all submissions'
              ' as argument!')
        sys.exit(1)
    if len(sys.argv) >= 4 and sys.argv[2] == '--reload':
        RESULTS = sys.argv[3]
        with open(RESULTS, 'r') as f:
            r = json.load(f)
            for k, v in r.items():
                RDICT[k] = v
    main(sys.argv[1])
