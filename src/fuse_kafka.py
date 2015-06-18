#!/usr/bin/env python
# chkconfig: 2345 11 88
### BEGIN INIT INFO
# Provides: fuse_kafka
# Required-Start:
# Required-Stop:
# Default-Start:  3 5    
# Default-Stop:
# Short-Description: run fuse_kafka
# Description:
### END INIT INFO
""" @package fuse_kafka
Startup script for fuse_kafka.
"""
import sys, getopt, json, glob, os, subprocess, copy, time, subprocess, multiprocessing, fcntl
""" CONFIGURATIONS_PATHS is the list of paths where the init script
will look for configurations """
CONFIGURATIONS_PATHS = ["./conf/*", "/etc/fuse_kafka.conf", "/etc/*.txt"]
class Crontab:
    """ manages a crontab """
    def add_line_if_necessary(self, line):
        """ adds a line to a crontab if it is not there """
        crontab = subprocess.Popen(["crontab", "-l"],
                stdout = subprocess.PIPE).communicate()[0]
        if not line in crontab.split("\n"):
            subprocess.Popen(["crontab"], stdin = subprocess.PIPE).communicate(
                    input=crontab + line + "\n")
class Mountpoints:
    """Utility class to umount non-responding or non-writable
    mountpoints"""
    def access(self, path):
        """ non-blocking check that a path is accessible """
        p = multiprocessing.Process(target=os.access, args=(path, os.W_OK))
        p.start()
        p.join(2)
        if p.is_alive():
            p.terminate()
            p.join()
            return False
        return os.access(path, os.W_OK)
    def umount_non_accessible(self):
        """ for eac configured directory, checks if the directory is
        accessible. If it is not accessible 10 second after the first time
        it was not, umount it """
        for path in Configuration().conf['directories']:
            if not self.access(path):
                time.sleep(10)
                if not self.access(path):
                    subprocess.call(["fusermount", "-uz", path])
class Configuration:
    """ Utility class to load configurations from properties files """
    def get_property(self, path, name):
        """ Get a property from a well defined property file.

        path - configuration file path
        name - property name

        Returns the first property value found in the given path with the
        given name, None if it was not found
        """
        with open(path) as f:
            for line in f.readlines():
                line = line.split('=', 1)
                if len(line) == 2 and line[0] == name:
                    return line[1].strip()
    def includes_subdir(self, dirs, subdir):
        """ Checks if a subdirectory is included in a list of prefix.

        dirs    - list of prefixes
        subdir  - path to check for prefix

        Returns True if dirs contains a prefix of subdir, False
        otherwise.
        """
        for dir in dirs:
            if subdir.startswith(dir):
                return True
        return False
    def exclude_directories(self, paths, prefixes):
        """ Exclude directories from a list of directories based on
        prefixes

        paths       - list of paths from which to exclude prefixs
        prefixes    - list of prefixes to exclude

        Returns the path list with excluded directories
        """
        return [path for path in paths if not self.includes_subdir(prefixes,
            os.path.realpath(path))]
    def exclude_from_conf(self, paths):
        self.conf['directories'] = self.exclude_directories(
                self.conf['directories'], paths)
    def __init__(self, configurations = CONFIGURATIONS_PATHS):
        self.configurations = configurations
        self.sleeping = False
        self.load()
    def parse_line(self, line, conf):
        """ Parse a configuration line

        line - the line to parse
        conf - a dictionary which will be updated based on the parsing

        Returns the configuration updated configuration based on the
        line
        """
        line = line.split('=', 1)
        if len(line) == 2:
            key = line[0]
            if line[0].startswith('monitoring_logging_') \
                    or line[0].startswith('fuse_kafka_') \
                    or line[0] == 'monitoring_top_substitutions':
                key = key.replace('monitoring_', '')
                key = key.replace('fuse_kafka_', '')
                key = key.replace('logging_', '').replace('top_', '')
                if not key in conf.keys(): conf[key] = []
                parsed = json.loads(line[1])
                if type(parsed) is dict:
                    for parsed_key in parsed.keys():
                        conf[key].append(parsed_key)
                        conf[key].append(parsed[parsed_key])
                else:
                    conf[key].extend(parsed)
    def is_sleeping(self, var_run_path = "/var/run"):
        """ Returns True if fuse_kafka is in sleep mode """
        return os.path.exists(var_run_path + '/fuse_kafka_backup')
    def unique_directories(self, conf_directories):
        """ return a list with duplicates removed 
        (in absolute path) from conf_directories """
        directories = []
        abstract_directories = []
        for directory in conf_directories:
            abstract_directory = os.path.abspath(directory)
            if not os.path.abspath(directory) in abstract_directories:
                directories.append(directory)
                abstract_directories.append(abstract_directory)
        return directories
    def load(self, var_run_path = "/var/run"):
        """ Loads configuration from configurations files """
        self.conf = {}
        for globbed in self.configurations:
            for config in glob.glob(globbed):
                with open(config) as f:
                    for line in f.readlines():
                        self.parse_line(line, self.conf)
        if self.is_sleeping(var_run_path):
            self.exclude_from_conf(self.conf['sleep'])
        self.conf['directories'] = self.unique_directories(self.conf['directories'])
        if 'sleep' in self.conf: del self.conf['sleep']
    def args(self):
        """ Returns the fuse_kafka binary arguments based on the
        parsed configuration """
        result = []
        for key in self.conf.keys():
            result.append('--' + str(key))
            for item in self.conf[key]:
                result.append(str(item))
        return result
    def __str__(self):
        return " ".join(self.args())
class FuseKafkaService:
    """ Utility class to run multiple fuse_kafka processes as one service """
    def __init__(self):
        self.prefix = ["fuse_kafka", "_", "-oallow_other",
                "-ononempty", "-omodules=subdir,subdir=.", "-f", "--"]
        self.proc_mount_path = "/proc/mounts"
        if "FUSE_KAFKA_PREFIX" in os.environ:
            self.prefix = os.environ["FUSE_KAFKA_PREFIX"].split() + self.prefix
    def do(self, action):
        """ Actually run an action 

        action - the action name

        """
        getattr(self, action)()
    def start(self):
        if self.get_status() == 0:
            print("fuse_kafka is already running")
            return
        self.start_excluding_directories([])
    def start_excluding_directories(self, excluded):
        """ Starts fuse_kafka processes """
        env = os.environ.copy()
        env["PATH"] = ".:" + env["PATH"]
        env["LD_LIBRARY_PATH"] = ":/usr/lib"
        self.configuration = Configuration()
        self.configuration.exclude_from_conf(excluded)
        directories = copy.deepcopy(self.configuration.conf['directories'])
        subprocess.call(["/sbin/modprobe", "fuse"])
        for directory in directories:
            print("starting fuse_kafka on " + directory)
            if not os.path.exists(directory):
                os.makedirs(directory)
        subprocess.Popen(self.prefix + self.configuration.args(), env = env)
        if self.get_status() == 0:
            print("fuse_kafka started")
    def stop(self):
        """ Stops fuse_kafka processes """
        for dir in self.list_watched_directories():
            subprocess.call(["fusermount", "-uz", dir])
        subprocess.call(["pkill", "-f", " ".join(self.prefix)])
        if self.get_status() != 0:
            print("fuse_kafka stoped")
    def reload(self, var_run_path = "/var/run"):
        """ if fuse_kafka is running, reloads the dynamic part of 
        the configuration. If it is not running, starts it """
        if self.get_status() == 3: self.start()
        else:
            self.configuration = Configuration()
            with open(var_run_path + "/fuse_kafka.args", "w") as f:
                fcntl.fcntl(f, fcntl.LOCK_EX)
                f.write(str(self.configuration))
            watched_directories = self.list_watched_directories()
            self.start_excluding_directories(watched_directories)
            for to_stop_watching in [a for a in watched_directories 
                    if a not in self.configuration.conf['directories']]:
                self.stop_watching_directory(to_stop_watching)
    def stop_watching_directory(self, to_stop_watching):
        """ Stops fuse_kafka process for a specific directory """
        subprocess.call(["fusermount", "-f", " ".join(self.prefix)])
        if self.get_status() != 0:
            print("fuse_kafka stoped")
    def restart(self):
        """ Stops and starts fuse_kafka processes """
        self.stop()
        while self.get_status() == 0: time.sleep(0.1)
        self.start()
    def list_watched_directories(self):
        result = []
        with open(self.proc_mount_path) as f:
            for line in f.readlines():
                if line.startswith("fuse_kafka"):
                    result.append(line.split()[1])
        return result
    def get_status(self):
        """ Displays the status of fuse_kafka processes """
        status = 3
        for directory in self.list_watched_directories():
            print("listening on " + directory)
            status = 0
        return status
    def status(self):
        status = self.get_status()
        sys.stdout.write("service is ")
        if status == 3: sys.stdout.write("not ")
        print("running")
        sys.exit(status) 
    def cleanup(self):
        """ if a fuse kafka mountpoint is not accessible, umount it.
        Also installs this action in the crontab so it is launched
        every minute. """
        Crontab().add_line_if_necessary("* * * * * " + os.path.realpath(__file__) + " cleanup")
        Mountpoints().umount_non_accessible()
if __name__ == "__main__":
    FuseKafkaService().do(sys.argv[1])
