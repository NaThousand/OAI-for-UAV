#/*
# * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
# * contributor license agreements.  See the NOTICE file distributed with
# * this work for additional information regarding copyright ownership.
# * The OpenAirInterface Software Alliance licenses this file to You under
# * the OAI Public License, Version 1.1  (the "License"); you may not use this file
# * except in compliance with the License.
# * You may obtain a copy of the License at
# *
# *      http://www.openairinterface.org/?page_id=698
# *
# * Unless required by applicable law or agreed to in writing, software
# * distributed under the License is distributed on an "AS IS" BASIS,
# * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# * See the License for the specific language governing permissions and
# * limitations under the License.
# *-------------------------------------------------------------------------------
# * For more information about the OpenAirInterface (OAI) Software Alliance:
# *      contact@openairinterface.org
# */
#---------------------------------------------------------------------
# 
#
#   Required Python Version
#     Python 3.x
#
#   Required Python Package
#     pexpect
#---------------------------------------------------------------------


#-----------------------------------------------------------
# Import Libs
#-----------------------------------------------------------
import sys		# arg
import re		# reg
import pexpect	# pexpect
import time		# sleep
import os
import subprocess
import xml.etree.ElementTree as ET
import logging
import datetime
import signal
import statistics as stat
from multiprocessing import SimpleQueue, Lock
import concurrent.futures
import json

#import our libs
import helpreadme as HELP
import constants as CONST
import cls_cluster as OC
import sshconnection

import cls_module
import cls_cmd

logging.getLogger("matplotlib").setLevel(logging.WARNING)
import matplotlib.pyplot as plt
import numpy as np

#-----------------------------------------------------------
# Helper functions used here and in other classes
#-----------------------------------------------------------
def Iperf_ComputeModifiedBW(idx, ue_num, profile, args):
	result = re.search('-b\s*(?P<iperf_bandwidth>[0-9\.]+)(?P<unit>[KMG])', str(args))
	if result is None:
		raise ValueError(f'requested iperf bandwidth not found in iperf options "{args}"')
	iperf_bandwidth = float(result.group('iperf_bandwidth'))
	if iperf_bandwidth == 0:
		raise ValueError('iperf bandwidth set to 0 - invalid value')
	if profile == 'balanced':
		iperf_bandwidth_new = iperf_bandwidth/ue_num
	if profile =='single-ue':
		iperf_bandwidth_new = iperf_bandwidth
	if profile == 'unbalanced':
		# residual is 2% of max bw
		residualBW = iperf_bandwidth / 50
		if idx == 0:
			iperf_bandwidth_new = iperf_bandwidth - ((ue_num - 1) * residualBW)
		else:
			iperf_bandwidth_new = residualBW
	iperf_bandwidth_str = result.group(0)
	iperf_bandwidth_unit = result.group(2)
	iperf_bandwidth_str_new = f"-b {'%.2f' % iperf_bandwidth_new}{iperf_bandwidth_unit}"
	args_new = re.sub(iperf_bandwidth_str, iperf_bandwidth_str_new, str(args))
	if iperf_bandwidth_unit == 'K':
		iperf_bandwidth_new = iperf_bandwidth_new / 1000
	return iperf_bandwidth_new, args_new

def Iperf_ComputeTime(args):
	result = re.search('-t\s*(?P<iperf_time>\d+)', str(args))
	if result is None:
		raise Exception('Iperf time not found!')
	return int(result.group('iperf_time'))

def Iperf_analyzeV3TCPJson(filename, iperf_tcp_rate_target):
	if (not os.path.isfile(filename)):
		return (False, 'Iperf3 TCP: Log file not present')
	if (os.path.getsize(filename)==0):
		return (False, 'Iperf3 TCP: Log file is empty')

	with open(filename) as file:
		filename = json.load(file)
		try:
			sender_bitrate   = round(filename['end']['streams'][0]['sender']['bits_per_second']/1000000,2)
			receiver_bitrate = round(filename['end']['streams'][0]['receiver']['bits_per_second']/1000000,2)
		except Exception as e:
			return (False, 'Could not compute Iperf3 bitrate!')

	snd_msg = f'Sender Bitrate   : {sender_bitrate} Mbps'
	rcv_msg = f'Receiver Bitrate : {receiver_bitrate} Mbps'
	success = True
	if (iperf_tcp_rate_target is not None):
		if (int(receiver_bitrate) < int(iperf_tcp_rate_target)):
			rcv_msg += f" (too low! < {iperf_tcp_rate_target} Mbps)"
			success = False
		else:
			rcv_msg += f" (target : {iperf_tcp_rate_target} Mbps)"
	return(success, f'{snd_msg}\n{rcv_msg}')

def Iperf_analyzeV3BIDIRJson(filename):
	if (not os.path.isfile(filename)):
		return (False, 'Iperf3 Bidir TCP: Log file not present')
	if (os.path.getsize(filename)==0):
		return (False, 'Iperf3 Bidir TCP: Log file is empty')

	with open(filename) as file:
		filename = json.load(file)
		try:
			sender_bitrate_ul   = round(filename['end']['streams'][0]['sender']['bits_per_second']/1000000,2)
			receiver_bitrate_ul = round(filename['end']['streams'][0]['receiver']['bits_per_second']/1000000,2)
			sender_bitrate_dl   = round(filename['end']['streams'][1]['sender']['bits_per_second']/1000000,2)
			receiver_bitrate_dl = round(filename['end']['streams'][1]['receiver']['bits_per_second']/1000000,2)
		except Exception as e:
			return (False, 'Could not compute BIDIR bitrate!')

	msg = f'Sender Bitrate DL   : {sender_bitrate_dl} Mbps\n'
	msg += f'Receiver Bitrate DL : {receiver_bitrate_dl} Mbps\n'
	msg += f'Sender Bitrate UL   : {sender_bitrate_ul} Mbps\n'
	msg += f'Receiver Bitrate UL : {receiver_bitrate_ul} Mbps\n'
	return (True, msg)

def Iperf_analyzeV3UDP(filename, iperf_bitrate_threshold, iperf_packetloss_threshold, target_bitrate):
	if (not os.path.isfile(filename)):
		return (False, 'Iperf3 UDP: Log file not present')
	if (os.path.getsize(filename)==0):
		return (False, 'Iperf3 UDP: Log file is empty')
	sender_bitrate = None
	receiver_bitrate = None
	with open(filename, 'r') as server_file:
		for line in server_file.readlines():
			res_sender = re.search(r'(?P<bitrate>[0-9\.]+)\s+(?P<unit>[KMG]bits\/sec)\s+(?P<jitter>[0-9\.]+\s+ms)\s+(?P<lostPack>\d+)/(?P<sentPack>\d+) \((?P<lost>[0-9\.]+).*?\s+(sender)', line)
			res_receiver = re.search(r'(?P<bitrate>[0-9\.]+)\s+(?P<unit>[KMG]bits\/sec)\s+(?P<jitter>[0-9\.]+\s+ms)\s+(?P<lostPack>\d+)/(?P<receivedPack>\d+) \((?P<lost>[0-9\.]+).*?\s+(receiver)', line)
			if res_sender is not None:
				sender_bitrate = res_sender.group('bitrate')
				sender_unit = res_sender.group('unit')
				sender_jitter = res_sender.group('jitter')
				sender_lostPack = res_sender.group('lostPack')
				sender_sentPack = res_sender.group('sentPack')
				sender_packetloss = res_sender.group('lost')
			if res_receiver is not None:
				receiver_bitrate = res_receiver.group('bitrate')
				receiver_unit = res_receiver.group('unit')
				receiver_jitter = res_receiver.group('jitter')
				receiver_lostPack = res_receiver.group('lostPack')
				receiver_receivedPack = res_receiver.group('receivedPack')
				receiver_packetloss = res_receiver.group('lost')

	if receiver_bitrate is not None and sender_bitrate is not None:
		if sender_unit == 'Kbits/sec':
			sender_bitrate = float(sender_bitrate) / 1000
		if receiver_unit == 'Kbits/sec':
			receiver_bitrate = float(receiver_bitrate) / 1000
		br_perf = 100 * float(receiver_bitrate) / float(target_bitrate)
		br_perf = '%.2f ' % br_perf
		sender_bitrate = '%.2f ' % float(sender_bitrate)
		receiver_bitrate = '%.2f ' % float(receiver_bitrate)
		req_msg = f'Sender Bitrate  : {sender_bitrate} Mbps'
		bir_msg = f'Receiver Bitrate: {receiver_bitrate} Mbps'
		brl_msg = f'{br_perf}%'
		jit_msg = f'Jitter          : {receiver_jitter}'
		pal_msg = f'Packet Loss     : {receiver_packetloss} %'
		if float(br_perf) < float(iperf_bitrate_threshold):
			brl_msg = f'too low! < {iperf_bitrate_threshold}%'
		if float(receiver_packetloss) > float(iperf_packetloss_threshold):
			pal_msg += f' (too high! > {iperf_packetloss_threshold}%)'
		result = float(br_perf) >= float(iperf_bitrate_threshold) and float(receiver_packetloss) <= float(iperf_packetloss_threshold)
		return (result, f'{req_msg}\n{bir_msg} ({brl_msg})\n{jit_msg}\n{pal_msg}')
	else:
		return (False, 'Could not analyze iperf report')

def Iperf_analyzeV2UDP(server_filename, iperf_bitrate_threshold, iperf_packetloss_threshold, target_bitrate):
		result = None
		if (not os.path.isfile(server_filename)):
			return (False, 'Iperf UDP: Server report not found!')
		if (os.path.getsize(server_filename)==0):
			return (False, 'Iperf UDP: Log file is empty')
		# Computing the requested bandwidth in float
		statusTemplate = r'(?:|\[ *\d+\].*) +0\.0-\s*(?P<duration>[0-9\.]+) +sec +[0-9\.]+ [kKMG]Bytes +(?P<bitrate>[0-9\.]+) (?P<magnitude>[kKMG])bits\/sec +(?P<jitter>[0-9\.]+) ms +(\d+\/ *\d+) +(\((?P<packetloss>[0-9\.]+)%\))'
		with open(server_filename, 'r') as server_file:
			for line in server_file.readlines():
				result = re.search(statusTemplate, str(line))
				if result is not None:
					break
		if result is None:
			return (False, 'Could not parse server report!')
		bitrate = float(result.group('bitrate'))
		magn = result.group('magnitude')
		if magn == "k" or magn == "K":
			bitrate /= 1000
		elif magn == "G": # we assume bitrate in Mbps, therefore it must be G now
			bitrate *= 1000
		jitter = float(result.group('jitter'))
		packetloss = float(result.group('packetloss'))
		br_perf = float(bitrate)/float(target_bitrate) * 100
		br_perf = '%.2f ' % br_perf

		result = float(br_perf) >= float(iperf_bitrate_threshold) and float(packetloss) <= float(iperf_packetloss_threshold)
		req_msg = f'Req Bitrate : {target_bitrate}'
		bir_msg = f'Bitrate     : {bitrate}'
		brl_msg = f'Bitrate Perf: {br_perf} %'
		if float(br_perf) < float(iperf_bitrate_threshold):
			brl_msg += f' (too low! <{iperf_bitrate_threshold}%)'
		jit_msg = f'Jitter      : {jitter}'
		pal_msg = f'Packet Loss : {packetloss}'
		if float(packetloss) > float(iperf_packetloss_threshold):
			pal_msg += f' (too high! >{self.iperf_packetloss_threshold}%)'
		return (result, f'{req_msg}\n{bir_msg}\n{brl_msg}\n{jit_msg}\n{pal_msg}')

#-----------------------------------------------------------
# OaiCiTest Class Definition
#-----------------------------------------------------------
class OaiCiTest():
	
	def __init__(self):
		self.ranRepository = ''
		self.ranBranch = ''
		self.ranCommitID = ''
		self.ranAllowMerge = False
		self.ranTargetBranch = ''

		self.FailReportCnt = 0
		self.testCase_id = ''
		self.testXMLfiles = []
		self.testUnstable = False
		self.testMinStableId = '999999'
		self.testStabilityPointReached = False
		self.desc = ''
		self.ping_args = ''
		self.ping_packetloss_threshold = ''
		self.ping_rttavg_threshold =''
		self.iperf_args = ''
		self.iperf_packetloss_threshold = ''
		self.iperf_bitrate_threshold = ''
		self.iperf_profile = ''
		self.iperf_options = ''
		self.iperf_tcp_rate_target = ''
		self.nbMaxUEtoAttach = -1
		self.UEDevices = []
		self.UEDevicesStatus = []
		self.UEDevicesRemoteServer = []
		self.UEDevicesRemoteUser = []
		self.UEDevicesOffCmd = []
		self.UEDevicesOnCmd = []
		self.UEDevicesRebootCmd = []
		self.idle_sleep_time = 0
		self.x2_ho_options = 'network'
		self.x2NbENBs = 0
		self.x2ENBBsIds = []
		self.x2ENBConnectedUEs = []
		self.repeatCounts = []
		self.finalStatus = False
		self.UEIPAddress = ''
		self.UEUserName = ''
		self.UEPassword = ''
		self.UE_instance = 0
		self.UESourceCodePath = ''
		self.UELogFile = ''
		self.Build_OAI_UE_args = ''
		self.Initialize_OAI_UE_args = ''
		self.clean_repository = True
		self.air_interface=''
		self.ue_ids = []
		self.svr_id = None
		self.cmd_prefix = '' # prefix before {lte,nr}-uesoftmodem


	def BuildOAIUE(self,HTML):
		if self.UEIPAddress == '' or self.ranRepository == '' or self.ranBranch == '' or self.UEUserName == '' or self.UEPassword == '' or self.UESourceCodePath == '':
			HELP.GenericHelp(CONST.Version)
			sys.exit('Insufficient Parameter')
		SSH = sshconnection.SSHConnection()
		SSH.open(self.UEIPAddress, self.UEUserName, self.UEPassword)
		result = re.search('--nrUE', self.Build_OAI_UE_args)
		if result is not None:
			self.air_interface='nr-uesoftmodem'
			ue_prefix = 'NR '
		else:
			self.air_interface='lte-uesoftmodem'
			ue_prefix = ''
		result = re.search('([a-zA-Z0-9\:\-\.\/])+\.git', self.ranRepository)
		if result is not None:
			full_ran_repo_name = self.ranRepository.replace('git/', 'git')
		else:
			full_ran_repo_name = self.ranRepository + '.git'
		SSH.command(f'mkdir -p {self.UESourceCodePath}', '\$', 5)
		SSH.command(f'cd {self.UESourceCodePath}', '\$', 5)
		SSH.command(f'if [ ! -e .git ]; then stdbuf -o0 git clone {full_ran_repo_name} .; else stdbuf -o0 git fetch --prune; fi', '\$', 600)
		# here add a check if git clone or git fetch went smoothly
		SSH.command('git config user.email "jenkins@openairinterface.org"', '\$', 5)
		SSH.command('git config user.name "OAI Jenkins"', '\$', 5)
		if self.clean_repository:
			SSH.command('ls *.txt', '\$', 5)
			result = re.search('LAST_BUILD_INFO', SSH.getBefore())
			if result is not None:
				mismatch = False
				SSH.command('grep --colour=never SRC_COMMIT LAST_BUILD_INFO.txt', '\$', 2)
				result = re.search(self.ranCommitID, SSH.getBefore())
				if result is None:
					mismatch = True
				SSH.command('grep --colour=never MERGED_W_TGT_BRANCH LAST_BUILD_INFO.txt', '\$', 2)
				if self.ranAllowMerge:
					result = re.search('YES', SSH.getBefore())
					if result is None:
						mismatch = True
					SSH.command('grep --colour=never TGT_BRANCH LAST_BUILD_INFO.txt', '\$', 2)
					if self.ranTargetBranch == '':
						result = re.search('develop', SSH.getBefore())
					else:
						result = re.search(self.ranTargetBranch, SSH.getBefore())
					if result is None:
						mismatch = True
				else:
					result = re.search('NO', SSH.getBefore())
					if result is None:
						mismatch = True
				if not mismatch:
					SSH.close()
					HTML.CreateHtmlTestRow(self.Build_OAI_UE_args, 'OK', CONST.ALL_PROCESSES_OK)
					return

			SSH.command(f'echo {self.UEPassword} | sudo -S git clean -x -d -ff', '\$', 30)

		# if the commit ID is provided use it to point to it
		if self.ranCommitID != '':
			SSH.command(f'git checkout -f {self.ranCommitID}', '\$', 30)
		# if the branch is not develop, then it is a merge request and we need to do 
		# the potential merge. Note that merge conflicts should already been checked earlier
		if self.ranAllowMerge:
			if self.ranTargetBranch == '':
				if (self.ranBranch != 'develop') and (self.ranBranch != 'origin/develop'):
					SSH.command('git merge --ff origin/develop -m "Temporary merge for CI"', '\$', 30)
			else:
				logging.debug(f'Merging with the target branch: {self.ranTargetBranch}')
				SSH.command(f'git merge --ff origin/{self.ranTargetBranch} -m "Temporary merge for CI"', '\$', 30)
		SSH.command('source oaienv', '\$', 5)
		SSH.command('cd cmake_targets', '\$', 5)
		SSH.command('mkdir -p log', '\$', 5)
		SSH.command('chmod 777 log', '\$', 5)
		# no need to remove in log (git clean did the trick)
		SSH.command(f'stdbuf -o0 ./build_oai {self.Build_OAI_UE_args} 2>&1 | stdbuf -o0 tee compile_oai_ue.log', 'Bypassing the Tests|build have failed', 1200)
		SSH.command('ls ran_build/build', '\$', 3)
		SSH.command('ls ran_build/build', '\$', 3)
		buildStatus = True
		result = re.search(self.air_interface, SSH.getBefore())
		if result is None:
			buildStatus = False
		SSH.command(f'mkdir -p build_log_{self.testCase_id}', '\$', 5)
		SSH.command(f'mv log/* build_log_{self.testCase_id}', '\$', 5)
		SSH.command(f'mv compile_oai_ue.log build_log_{self.testCase_id}', '\$', 5)
		if buildStatus:
			# Generating a BUILD INFO file
			SSH.command(f'echo "SRC_BRANCH: {self.ranBranch}" > ../LAST_BUILD_INFO.txt', '\$', 2)
			SSH.command(f'echo "SRC_COMMIT: {self.ranCommitID}" >> ../LAST_BUILD_INFO.txt', '\$', 2)
			if self.ranAllowMerge:
				SSH.command('echo "MERGED_W_TGT_BRANCH: YES" >> ../LAST_BUILD_INFO.txt', '\$', 2)
				if self.ranTargetBranch == '':
					SSH.command('echo "TGT_BRANCH: develop" >> ../LAST_BUILD_INFO.txt', '\$', 2)
				else:
					SSH.command(f'echo "TGT_BRANCH: {self.ranTargetBranch}" >> ../LAST_BUILD_INFO.txt', '\$', 2)
			else:
				SSH.command('echo "MERGED_W_TGT_BRANCH: NO" >> ../LAST_BUILD_INFO.txt', '\$', 2)
			SSH.close()
			HTML.CreateHtmlTestRow(self.Build_OAI_UE_args, 'OK', CONST.ALL_PROCESSES_OK, 'OAI UE')
		else:
			SSH.close()
			logging.error('\u001B[1m Building OAI UE Failed\u001B[0m')
			HTML.CreateHtmlTestRow(self.Build_OAI_UE_args, 'KO', CONST.ALL_PROCESSES_OK, 'OAI UE')
			HTML.CreateHtmlTabFooter(False)
			self.ConditionalExit()


	def InitializeUE(self, HTML):
		ues = [cls_module.Module_UE(n.strip()) for n in self.ue_ids]
		messages = []
		with concurrent.futures.ThreadPoolExecutor() as executor:
			futures = [executor.submit(ue.initialize) for ue in ues]
			for f, ue in zip(futures, ues):
				uename = f'UE {ue.getName()}'
				messages.append(f'{uename}: initialized' if f.result() else f'{uename}: ERROR during Initialization')
			[f.result() for f in futures]
		HTML.CreateHtmlTestRowQueue('N/A', 'OK', messages)

	def InitializeOAIUE(self,HTML,RAN,EPC,CONTAINERS):
		if self.UEIPAddress == '' or self.UEUserName == '' or self.UEPassword == '' or self.UESourceCodePath == '':
			HELP.GenericHelp(CONST.Version)
			sys.exit('Insufficient Parameter')

			
		if self.air_interface == 'lte-uesoftmodem':
			result = re.search('--no-L2-connect', str(self.Initialize_OAI_UE_args))
			if result is None:
				check_eNB = True
				check_OAI_UE = False
			UE_prefix = ''
		else:
			UE_prefix = 'NR '
		SSH = sshconnection.SSHConnection()
		SSH.open(self.UEIPAddress, self.UEUserName, self.UEPassword)
		SSH.command(f'cd {self.UESourceCodePath}', '\$', 5)
		# Initialize_OAI_UE_args usually start with -C and followed by the location in repository
		SSH.command('source oaienv', '\$', 5)
		SSH.command('cd cmake_targets/ran_build/build', '\$', 5)
		if self.air_interface == 'lte-uesoftmodem':
			result = re.search('--no-L2-connect', str(self.Initialize_OAI_UE_args))
			# We may have to regenerate the .u* files
			if result is None:
				SSH.command('ls /tmp/*.sed', '\$', 5)
				result = re.search('adapt_usim_parameters', SSH.getBefore())
				if result is not None:
					SSH.command('sed -f /tmp/adapt_usim_parameters.sed ../../../openair3/NAS/TOOLS/ue_eurecom_test_sfr.conf > ../../../openair3/NAS/TOOLS/ci-ue_eurecom_test_sfr.conf', '\$', 5)
				else:
					SSH.command('sed -e "s#93#92#" -e "s#8baf473f2f8fd09487cccbd7097c6862#fec86ba6eb707ed08905757b1bb44b8f#" -e "s#e734f8734007d6c5ce7a0508809e7e9c#C42449363BBAD02B66D16BC975D77CC1#" ../../../openair3/NAS/TOOLS/ue_eurecom_test_sfr.conf > ../../../openair3/NAS/TOOLS/ci-ue_eurecom_test_sfr.conf', '\$', 5)
				SSH.command(f'echo {self.UEPassword} | sudo -S rm -Rf .u*', '\$', 5)
				SSH.command(f'echo {self.UEPassword} | sudo -S ../../nas_sim_tools/build/conf2uedata -c ../../../openair3/NAS/TOOLS/ci-ue_eurecom_test_sfr.conf -o .', '\$', 5)
		else:
			SSH.command(f'if [ -e rbconfig.raw ]; then echo {self.UEPassword} | sudo -S rm rbconfig.raw; fi', '\$', 5)
			SSH.command(f'if [ -e reconfig.raw ]; then echo {self.UEPassword} | sudo -S rm reconfig.raw; fi', '\$', 5)
			# Copy the RAW files from gNB running directory (maybe on another machine)
			copyin_res = SSH.copyin(RAN.eNBIPAddress, RAN.eNBUserName, RAN.eNBPassword, RAN.eNBSourceCodePath + '/cmake_targets/rbconfig.raw', '.')
			if (copyin_res == 0):
				SSH.copyout(self.UEIPAddress, self.UEUserName, self.UEPassword, './rbconfig.raw', self.UESourceCodePath + '/cmake_targets/ran_build/build')
			copyin_res = SSH.copyin(RAN.eNBIPAddress, RAN.eNBUserName, RAN.eNBPassword, RAN.eNBSourceCodePath + '/cmake_targets/reconfig.raw', '.')
			if (copyin_res == 0):
				SSH.copyout(self.UEIPAddress, self.UEUserName, self.UEPassword, './reconfig.raw', self.UESourceCodePath + '/cmake_targets/ran_build/build')
		SSH.command(f'echo "ulimit -c unlimited && {self.cmd_prefix} ./{self.air_interface} {self.Initialize_OAI_UE_args}" > ./my-lte-uesoftmodem-run{self.UE_instance}.sh', '\$', 5)
		SSH.command(f'chmod 775 ./my-lte-uesoftmodem-run {self.UE_instance}.sh', '\$', 5)
		SSH.command(f'echo {self.UEPassword} | sudo -S rm -Rf {self.UESourceCodePath}/cmake_targets/ue_{self.testCase_id}.log', '\$', 5)
		self.UELogFile = f'ue_{self.testCase_id}.log'

		# We are now looping several times to hope we really sync w/ an eNB
		doOutterLoop = True
		outterLoopCounter = 5
		gotSyncStatus = True
		fullSyncStatus = True
		while (doOutterLoop):
			SSH.command(f'cd {self.UESourceCodePath}/cmake_targets/ran_build/build', '\$', 5)
			SSH.command(f'echo {self.UEPassword} | sudo -S rm -Rf {self.UESourceCodePath}/cmake_targets/ue_{self.testCase_id}.log', '\$', 5)
			SSH.command(f'echo $USER; nohup sudo -E stdbuf -o0 ./my-lte-uesoftmodem-run {self.UE_instance}.sh > {self.UESourceCodePath}/cmake_targets/ue_{self.testCase_id}.log 2>&1 &', self.UEUserName, 5)
			time.sleep(6)
			SSH.command('cd ../..', '\$', 5)
			doLoop = True
			loopCounter = 10
			gotSyncStatus = True
			# the 'got sync' message is for the UE threads synchronization
			while (doLoop):
				loopCounter = loopCounter - 1
				if (loopCounter == 0):
					# Here should never occur
					logging.error('"got sync" message never showed!')
					gotSyncStatus = False
					doLoop = False
					continue
				SSH.command(f'stdbuf -o0 cat ue_{self.testCase_id}.log | egrep --text --color=never -i "wait|sync"', '\$', 4)
				if self.air_interface == 'nr-uesoftmodem':
					result = re.search('Starting sync detection', SSH.getBefore())
				else:
					result = re.search('got sync', SSH.getBefore())
				if result is None:
					time.sleep(10)
				else:
					doLoop = False
					logging.debug('Found "got sync" message!')
			if gotSyncStatus == False:
				# we certainly need to stop the lte-uesoftmodem process if it is still running!
				SSH.command('ps -aux | grep --text --color=never softmodem | grep -v grep', '\$', 4)
				result = re.search('-uesoftmodem', SSH.getBefore())
				if result is not None:
					SSH.command(f'echo {self.UEPassword} | sudo -S killall --signal=SIGINT -r *-uesoftmodem', '\$', 4)
					time.sleep(3)
				outterLoopCounter = outterLoopCounter - 1
				if (outterLoopCounter == 0):
					doOutterLoop = False
				continue
			# We are now checking if sync w/ eNB DOES NOT OCCUR
			# Usually during the cell synchronization stage, the UE returns with No cell synchronization message
			# That is the case for LTE
			# In NR case, it's a positive message that will show if synchronization occurs
			doLoop = True
			if self.air_interface == 'nr-uesoftmodem':
				loopCounter = 10
			else:
				# We are now checking if sync w/ eNB DOES NOT OCCUR
				# Usually during the cell synchronization stage, the UE returns with No cell synchronization message
				loopCounter = 10
			while (doLoop):
				loopCounter = loopCounter - 1
				if (loopCounter == 0):
					if self.air_interface == 'nr-uesoftmodem':
						# Here we do have great chances that UE did NOT cell-sync w/ gNB
						doLoop = False
						fullSyncStatus = False
						logging.debug('Never seen the NR-Sync message (Measured Carrier Frequency) --> try again')
						time.sleep(6)
						# Stopping the NR-UE  
						SSH.command('ps -aux | grep --text --color=never softmodem | grep -v grep', '\$', 4)
						result = re.search('nr-uesoftmodem', SSH.getBefore())
						if result is not None:
							SSH.command(f'echo {self.UEPassword} | sudo -S killall --signal=SIGINT nr-uesoftmodem', '\$', 4)
						time.sleep(6)
					else:
						# Here we do have a great chance that the UE did cell-sync w/ eNB
						doLoop = False
						doOutterLoop = False
						fullSyncStatus = True
						continue
				SSH.command(f'stdbuf -o0 cat ue_{self.testCase_id}.log | egrep --text --color=never -i "wait|sync|Frequency"', '\$', 4)
				if self.air_interface == 'nr-uesoftmodem':
					# Positive messaging -->
					result = re.search('Measured Carrier Frequency', SSH.getBefore())
					if result is not None:
						doLoop = False
						doOutterLoop = False
						fullSyncStatus = True
					else:
						time.sleep(6)
				else:
					# Negative messaging -->
					result = re.search('No cell synchronization found', SSH.getBefore())
					if result is None:
						time.sleep(6)
					else:
						doLoop = False
						fullSyncStatus = False
						logging.debug('Found: "No cell synchronization" message! --> try again')
						time.sleep(6)
						SSH.command('ps -aux | grep --text --color=never softmodem | grep -v grep', '\$', 4)
						result = re.search('lte-uesoftmodem', SSH.getBefore())
						if result is not None:
							SSH.command(f'echo {self.UEPassword} | sudo -S killall --signal=SIGINT lte-uesoftmodem', '\$', 4)
			outterLoopCounter = outterLoopCounter - 1
			if (outterLoopCounter == 0):
				doOutterLoop = False

		if fullSyncStatus and gotSyncStatus:
			doInterfaceCheck = False
			if self.air_interface == 'lte-uesoftmodem':
				result = re.search('--no-L2-connect', str(self.Initialize_OAI_UE_args))
				if result is None:
					doInterfaceCheck = True
			# For the moment, only in explicit noS1 without kernel module (ie w/ tunnel interface)
			if self.air_interface == 'nr-uesoftmodem':
				result = re.search('--noS1', str(self.Initialize_OAI_UE_args))
				if result is not None:
					doInterfaceCheck = True
			if doInterfaceCheck:
				SSH.command('ifconfig oaitun_ue1', '\$', 4)
				SSH.command('ifconfig oaitun_ue1', '\$', 4)
				# ifconfig output is different between ubuntu 16 and ubuntu 18
				result = re.search('inet addr:[0-9]|inet [0-9]', SSH.getBefore())
				if result is not None:
					logging.debug('\u001B[1m oaitun_ue1 interface is mounted and configured\u001B[0m')
					tunnelInterfaceStatus = True
				else:
					logging.debug(SSH.getBefore())
					logging.error('\u001B[1m oaitun_ue1 interface is either NOT mounted or NOT configured\u001B[0m')
					tunnelInterfaceStatus = False
				if RAN.eNBmbmsEnables[0]:
					SSH.command('ifconfig oaitun_uem1', '\$', 4)
					result = re.search('inet addr', SSH.getBefore())
					if result is not None:
						logging.debug('\u001B[1m oaitun_uem1 interface is mounted and configured\u001B[0m')
						tunnelInterfaceStatus = tunnelInterfaceStatus and True
					else:
						logging.error('\u001B[1m oaitun_uem1 interface is either NOT mounted or NOT configured\u001B[0m')
						tunnelInterfaceStatus = False
			else:
				tunnelInterfaceStatus = True
		else:
			tunnelInterfaceStatus = True

		SSH.close()
		if fullSyncStatus and gotSyncStatus and tunnelInterfaceStatus:
			HTML.CreateHtmlTestRow(self.air_interface + ' ' + self.Initialize_OAI_UE_args, 'OK', CONST.ALL_PROCESSES_OK, 'OAI UE')
			logging.debug('\u001B[1m Initialize OAI UE Completed\u001B[0m')
		else:
			if self.air_interface == 'lte-uesoftmodem':
				if RAN.eNBmbmsEnables[0]:
					HTML.htmlUEFailureMsg='oaitun_ue1/oaitun_uem1 interfaces are either NOT mounted or NOT configured'
				else:
					HTML.htmlUEFailureMsg='oaitun_ue1 interface is either NOT mounted or NOT configured'
				HTML.CreateHtmlTestRow(self.air_interface + ' ' + self.Initialize_OAI_UE_args, 'KO', CONST.OAI_UE_PROCESS_NO_TUNNEL_INTERFACE, 'OAI UE')
			else:
				HTML.htmlUEFailureMsg='nr-uesoftmodem did NOT synced'
				HTML.CreateHtmlTestRow(self.air_interface + ' ' +  self.Initialize_OAI_UE_args, 'KO', CONST.OAI_UE_PROCESS_COULD_NOT_SYNC, 'OAI UE')
			logging.error('\033[91mInitialize OAI UE Failed! \033[0m')
			self.AutoTerminateUEandeNB(HTML,RAN,EPC,CONTAINERS)

	def AttachUE(self, HTML, RAN, EPC, CONTAINERS):
		ues = [cls_module.Module_UE(n.strip()) for n in self.ue_ids]
		with concurrent.futures.ThreadPoolExecutor() as executor:
			futures = [executor.submit(ue.attach) for ue in ues]
			attached = [f.result() for f in futures]
			futures = [executor.submit(ue.checkMTU) for ue in ues]
			mtus = [f.result() for f in futures]
			messages = [f"UE {ue.getName()}: {ue.getIP()}" for ue in ues]
		if all(attached) and all(mtus):
			HTML.CreateHtmlTestRowQueue('N/A', 'OK', messages)
		else:
			logging.error(f'error attaching or wrong MTU: attached {attached}, mtus {mtus}')
			HTML.CreateHtmlTestRowQueue('N/A', 'KO', ["Could not retrieve UE IP address(es) or MTU(s) wrong!"])
			self.AutoTerminateUEandeNB(HTML, RAN, EPC, CONTAINERS)

	def DetachUE(self, HTML):
		ues = [cls_module.Module_UE(n.strip()) for n in self.ue_ids]
		with concurrent.futures.ThreadPoolExecutor() as executor:
			futures = [executor.submit(ue.detach) for ue in ues]
			[f.result() for f in futures]
			messages = [f"UE {ue.getName()}: detached" for ue in ues]
		HTML.CreateHtmlTestRowQueue('NA', 'OK', messages)

	def DataDisableUE(self, HTML):
		ues = [cls_module.Module_UE(n.strip()) for n in self.ue_ids]
		with concurrent.futures.ThreadPoolExecutor() as executor:
			futures = [executor.submit(ue.dataDisable) for ue in ues]
			status = [f.result() for f in futures]
		if all(status):
			messages = [f"UE {ue.getName()}: data disabled" for ue in ues]
			HTML.CreateHtmlTestRowQueue('NA', 'OK', messages)
		else:
			logging.error(f'error enabling data: {status}')
			HTML.CreateHtmlTestRowQueue('N/A', 'KO', ["Could not disable UE data!"])

	def DataEnableUE(self, HTML):
		ues = [cls_module.Module_UE(n.strip()) for n in self.ue_ids]
		logging.debug(f'disabling data for UEs {ues}')
		with concurrent.futures.ThreadPoolExecutor() as executor:
			futures = [executor.submit(ue.dataEnable) for ue in ues]
			status = [f.result() for f in futures]
		if all(status):
			messages = [f"UE {ue.getName()}: data enabled" for ue in ues]
			HTML.CreateHtmlTestRowQueue('NA', 'OK', messages)
		else:
			logging.error(f'error enabling data: {status}')
			HTML.CreateHtmlTestRowQueue('N/A', 'KO', ["Could not enable UE data!"])

	def CheckStatusUE(self,HTML):
		ues = [cls_module.Module_UE(n.strip()) for n in self.ue_ids]
		logging.debug(f'checking status of UEs {ues}')
		messages = []
		with concurrent.futures.ThreadPoolExecutor() as executor:
			futures = [executor.submit(ue.check) for ue in ues]
			messages = [f.result() for f in futures]
		HTML.CreateHtmlTestRowQueue('NA', 'OK', messages)

	def Ping_common(self, EPC, ue, RAN, printLock):
		# Launch ping on the EPC side (true for ltebox and old open-air-cn)
		ping_status = 0
		ueIP = ue.getIP()
		if not ueIP:
			return (False, f"UE {ue.getName()} has no IP address")
		ping_log_file = f'ping_{self.testCase_id}_{ue.getName()}.log'
		ping_time = re.findall("-c *(\d+)",str(self.ping_args))
		local_ping_log_file = f'{os.getcwd()}/{ping_log_file}'
		# if has pattern %cn_ip%, replace with core IP address, else we assume the IP is present
		if re.search('%cn_ip%', self.ping_args):
			#target address is different depending on EPC type
			if re.match('OAI-Rel14-Docker', EPC.Type, re.IGNORECASE):
				self.ping_args = re.sub('%cn_ip%', EPC.MmeIPAddress, self.ping_args)
			elif re.match('OAICN5G', EPC.Type, re.IGNORECASE):
				self.ping_args = re.sub('%cn_ip%', EPC.MmeIPAddress, self.ping_args)
			elif re.match('OC-OAI-CN5G', EPC.Type, re.IGNORECASE):
				self.ping_args = re.sub('%cn_ip%', '172.21.6.100', self.ping_args)
			else:
				self.ping_args = re.sub('%cn_ip%', EPC.IPAddress, self.ping_args)
		#ping from module NIC rather than IP address to make sure round trip is over the air
		interface = f'-I {ue.getIFName()}' if ue.getIFName() else ''
		ping_cmd = f'{ue.getCmdPrefix()} ping {interface} {self.ping_args} 2>&1 | tee /tmp/{ping_log_file}'
		cmd = cls_cmd.getConnection(ue.getHost())
		response = cmd.run(ping_cmd, timeout=int(ping_time[0])*1.5)
		ue_header = f'UE {ue.getName()} ({ueIP})'
		if response.returncode != 0:
			message = ue_header + ': ping crashed: TIMEOUT?'
			logging.error('\u001B[1;37;41m ' + message + ' \u001B[0m')
			return (False, message)

		#copy the ping log file to have it locally for analysis (ping stats)
		cmd.copyin(src=f'/tmp/{ping_log_file}', tgt=local_ping_log_file)
		cmd.close()

		with open(local_ping_log_file, 'r') as f:
			ping_output = "".join(f.readlines())
		result = re.search(', (?P<packetloss>[0-9\.]+)% packet loss, time [0-9\.]+ms', ping_output)
		if result is None:
			message = ue_header + ': Packet Loss Not Found!'
			logging.error(f'\u001B[1;37;41m {message} \u001B[0m')
			return (False, message)
		packetloss = result.group('packetloss')
		result = re.search('rtt min\/avg\/max\/mdev = (?P<rtt_min>[0-9\.]+)\/(?P<rtt_avg>[0-9\.]+)\/(?P<rtt_max>[0-9\.]+)\/[0-9\.]+ ms', ping_output)
		if result is None:
			message = ue_header + ': Ping RTT_Min RTT_Avg RTT_Max Not Found!'
			logging.error(f'\u001B[1;37;41m {message} \u001B[0m')
			return (False, message)
		rtt_min = result.group('rtt_min')
		rtt_avg = result.group('rtt_avg')
		rtt_max = result.group('rtt_max')

		pal_msg = f'Packet Loss: {packetloss}%'
		min_msg = f'RTT(Min)   : {rtt_min} ms'
		avg_msg = f'RTT(Avg)   : {rtt_avg} ms'
		max_msg = f'RTT(Max)   : {rtt_max} ms'

		# adding a lock for cleaner display in command line
		printLock.acquire()
		logging.info(f'\u001B[1;37;44m ping result for {ue_header} \u001B[0m')
		logging.info(f'\u001B[1;34m    {pal_msg} \u001B[0m')
		logging.info(f'\u001B[1;34m    {min_msg} \u001B[0m')
		logging.info(f'\u001B[1;34m    {avg_msg} \u001B[0m')
		logging.info(f'\u001B[1;34m    {max_msg} \u001B[0m')

		message = f'{ue_header}\n{pal_msg}\n{min_msg}\n{avg_msg}\n{max_msg}'

		#checking packet loss compliance
		if float(packetloss) > float(self.ping_packetloss_threshold):
			message += '\nPacket Loss too high'
			logging.error(f'\u001B[1;37;41m Packet Loss too high; Target: {self.ping_packetloss_threshold}%\u001B[0m')
			printLock.release()
			return (False, message)
		elif float(packetloss) > 0:
			message += '\nPacket Loss is not 0%'
			logging.info('\u001B[1;30;43m Packet Loss is not 0% \u001B[0m')

		if self.ping_rttavg_threshold != '':
			if float(rtt_avg) > float(self.ping_rttavg_threshold):
				ping_rttavg_error_msg = f'RTT(Avg) too high: {rtt_avg} ms; Target: {self.ping_rttavg_threshold} ms'
				message += f'\n {ping_rttavg_error_msg}'
				logging.error('\u001B[1;37;41m'+ ping_rttavg_error_msg +' \u001B[0m')
				printLock.release()
				return (False, message)
		printLock.release()

		return (True, message)

	def Ping(self,HTML,RAN,EPC,CONTAINERS):
		if EPC.IPAddress == '' or EPC.UserName == '' or EPC.Password == '' or EPC.SourceCodePath == '':
			HELP.GenericHelp(CONST.Version)
			sys.exit('Insufficient Parameter')

		if self.ue_ids == []:
			raise Exception("no module names in self.ue_ids provided")

		ues = [cls_module.Module_UE(n.strip()) for n in self.ue_ids]
		logging.debug(ues)
		pingLock = Lock()
		with concurrent.futures.ThreadPoolExecutor() as executor:
			futures = [executor.submit(self.Ping_common, EPC, ue, RAN, pingLock) for ue in ues]
			results = [f.result() for f in futures]
			# each result in results is a tuple, first member goes to successes, second to messages
			successes, messages = map(list, zip(*results))
		if len(successes) == len(ues) and all(successes):
			HTML.CreateHtmlTestRowQueue(self.ping_args, 'OK', messages)
		else:
			HTML.CreateHtmlTestRowQueue(self.ping_args, 'KO', messages)
			self.AutoTerminateUEandeNB(HTML,RAN,EPC,CONTAINERS)

	def Iperf_Module(self, EPC, ue, svr, RAN, idx, ue_num, CONTAINERS):
		ueIP = ue.getIP()
		if not ueIP:
			return (False, f"UE {ue.getName()} has no IP address")
		svrIP = svr.getIP()
		if not svrIP:
			return (False, f"Iperf server {ue.getName()} has no IP address")

		runIperf3Server = svr.getRunIperf3Server()
		iperf_opt = self.iperf_args
		jsonReport = "--json"
		serverReport = ""
		udpIperf = re.search('-u', iperf_opt) is not None
		bidirIperf = re.search('--bidir', iperf_opt) is not None
		client_filename = f'iperf_client_{self.testCase_id}_{ue.getName()}.log'
		server_filename = f'iperf_server_{self.testCase_id}_{ue.getName()}.log'
		ymlPath = CONTAINERS.yamlPath[0].split('/')
		logPath = f'../cmake_targets/log/{ymlPath[1]}'

		if udpIperf:
			target_bitrate, iperf_opt = Iperf_ComputeModifiedBW(idx, ue_num, self.iperf_profile, self.iperf_args)
			# note: for UDP testing we don't want to use json report - reports 0 Mbps received bitrate
			jsonReport = ""
			# note: enable server report collection on the UE side, no need to store and collect server report separately on the server side
			serverReport = "--get-server-output"
			logging.info(f'iperf options modified from "{self.iperf_args}" to "{iperf_opt}" for {ue.getName()}')
		iperf_time = Iperf_ComputeTime(self.iperf_args)
		# hack: the ADB UEs don't have iperf in $PATH, so we need to hardcode for the moment
		iperf_ue = '/data/local/tmp/iperf3' if re.search('adb', ue.getName()) else 'iperf3'
		ue_header = f'UE {ue.getName()} ({ueIP})'

		if svr.getName() == "rfsim4g_enb_fembms":
			with cls_cmd.getConnection(ue.getHost()) as cmd_ue, cls_cmd.getConnection(EPC.IPAddress) as cmd_svr:
				port = 5002 + idx
				cmd_ue.run(f'{ue.getCmdPrefix()} iperf -B {ueIP} -s -u -i1 >> {server_filename} &', timeout=iperf_time*1.5)
				cmd_svr.run(f'{svr.getCmdPrefix()} iperf -c {ueIP} -B {svrIP} {iperf_opt} -i1 2>&1 | tee {client_filename}', timeout=iperf_time*2.5)
				cmd_ue.run(f'cp {client_filename} {logPath}/{client_filename}')
				cmd_ue.run(f'cp {server_filename} {logPath}/{server_filename}')
				status, msg = Iperf_analyzeV2UDP(server_filename, self.iperf_bitrate_threshold, self.iperf_packetloss_threshold, target_bitrate)
		else:
			with cls_cmd.getConnection(ue.getHost()) as cmd_ue, cls_cmd.getConnection(EPC.IPAddress) as cmd_svr:
				port = 5002 + idx
				# note: some core setups start an iperf3 server automatically, indicated in ci_infra by runIperf3Server: False`
				t = iperf_time * 2.5
				if runIperf3Server:
					cmd_svr.run(f'{svr.getCmdPrefix()} nohup timeout -vk3 {t} iperf3 -s -B {svrIP} -p {port} -1 {jsonReport} &', timeout=t)
				cmd_ue.run(f'rm /tmp/{client_filename}', reportNonZero=False)
				cmd_ue.run(f'{ue.getCmdPrefix()} timeout -vk3 {t} {iperf_ue} -B {ueIP} -c {svrIP} -p {port} {iperf_opt} {jsonReport} {serverReport} -O 5 >> /tmp/{client_filename}', timeout=t)
				if svr.getHost() == 'localhost':
					cmd_ue.run(f'mkdir -p {logPath}')
					cmd_ue.run(f'cp /tmp/{client_filename} {logPath}/{client_filename}')
					cmd_ue.run(f'cp /tmp/{client_filename} {client_filename}')
				else:
					cmd_ue.copyin(f'/tmp/{client_filename}', client_filename)
			if udpIperf:
				status, msg = Iperf_analyzeV3UDP(client_filename, self.iperf_bitrate_threshold, self.iperf_packetloss_threshold, target_bitrate)
			elif bidirIperf:
				status, msg = Iperf_analyzeV3BIDIRJson(client_filename)
			else:
				status, msg = Iperf_analyzeV3TCPJson(client_filename, self.iperf_tcp_rate_target)

		logging.info(f'\u001B[1;37;45m iperf result for {ue_header}\u001B[0m')
		for l in msg.split('\n'):
			logging.info(f'\u001B[1;35m    {l} \u001B[0m')
		return (status, f'{ue_header}\n{msg}')

	def IperfNoS1(self,HTML,RAN,EPC,CONTAINERS):
		raise 'IperfNoS1 not implemented'

	def Iperf(self,HTML,RAN,EPC,CONTAINERS):
		result = re.search('noS1', str(RAN.Initialize_eNB_args))
		if result is not None:
			self.IperfNoS1(HTML,RAN,EPC,CONTAINERS)
			return
		if EPC.IPAddress == '' or EPC.UserName == '' or EPC.Password == '' or EPC.SourceCodePath == '':
			HELP.GenericHelp(CONST.Version)
			sys.exit('Insufficient Parameter')

		logging.debug(f'Iperf: iperf_args "{self.iperf_args}" iperf_packetloss_threshold "{self.iperf_packetloss_threshold}" iperf_bitrate_threshold "{self.iperf_bitrate_threshold}" iperf_profile "{self.iperf_profile}" iperf_options "{self.iperf_options}"')

		if self.ue_ids == [] or self.svr_id == None:
			raise Exception("no module names in self.ue_ids or/and self.svr_id provided")

		ues = [cls_module.Module_UE(n.strip()) for n in self.ue_ids]
		svr = cls_module.Module_UE(self.svr_id)
		logging.debug(ues)
		with concurrent.futures.ThreadPoolExecutor() as executor:
			futures = [executor.submit(self.Iperf_Module, EPC, ue, svr, RAN, i, len(ues), CONTAINERS) for i, ue in enumerate(ues)]
			results = [f.result() for f in futures]
			# each result in results is a tuple, first member goes to successes, second to messages
			successes, messages = map(list, zip(*results))
		if len(successes) == len(ues) and all(successes):
			HTML.CreateHtmlTestRowQueue(self.iperf_args, 'OK', messages)
		else:
			HTML.CreateHtmlTestRowQueue(self.iperf_args, 'KO', messages)
			self.AutoTerminateUEandeNB(HTML,RAN,EPC,CONTAINERS)

	def AnalyzeLogFile_UE(self, UElogFile,HTML,RAN):
		if (not os.path.isfile(f'./{UElogFile}')):
			return -1
		ue_log_file = open(f'./{UElogFile}', 'r')
		exitSignalReceived = False
		foundAssertion = False
		msgAssertion = ''
		msgLine = 0
		foundSegFault = False
		foundRealTimeIssue = False
		uciStatMsgCount = 0
		pdcpDataReqFailedCount = 0
		badDciCount = 0
		f1aRetransmissionCount = 0
		fatalErrorCount = 0
		macBsrTimerExpiredCount = 0
		rrcConnectionRecfgComplete = 0
		no_cell_sync_found = False
		mib_found = False
		frequency_found = False
		plmn_found = False
		nrUEFlag = False
		nrDecodeMib = 0
		nrFoundDCI = 0
		nrCRCOK = 0
		mbms_messages = 0
		nbPduSessAccept = 0
		nbPduDiscard = 0
		HTML.htmlUEFailureMsg=''
		global_status = CONST.ALL_PROCESSES_OK
		for line in ue_log_file.readlines():
			result = re.search('nr_synchro_time|Starting NR UE soft modem', str(line))
			if result is not None:
				nrUEFlag = True
			if nrUEFlag:
				result = re.search('decode mib', str(line))
				if result is not None:
					nrDecodeMib += 1
				result = re.search('found 1 DCIs', str(line))
				if result is not None:
					nrFoundDCI += 1
				result = re.search('CRC OK', str(line))
				if result is not None:
					nrCRCOK += 1
				result = re.search('Received PDU Session Establishment Accept', str(line))
				if result is not None:
					nbPduSessAccept += 1
				result = re.search('warning: discard PDU, sn out of window', str(line))
				if result is not None:
					nbPduDiscard += 1
				result = re.search('--nfapi STANDALONE_PNF --node-number 2 --sa', str(line))
				if result is not None:
					frequency_found = True
			result = re.search('Exiting OAI softmodem', str(line))
			if result is not None:
				exitSignalReceived = True
			result = re.search('System error|[Ss]egmentation [Ff]ault|======= Backtrace: =========|======= Memory map: ========', str(line))
			if result is not None and not exitSignalReceived:
				foundSegFault = True
			result = re.search('[Cc]ore [dD]ump', str(line))
			if result is not None and not exitSignalReceived:
				foundSegFault = True
			result = re.search('[Aa]ssertion', str(line))
			if result is not None and not exitSignalReceived:
				foundAssertion = True
			result = re.search('LLL', str(line))
			if result is not None and not exitSignalReceived:
				foundRealTimeIssue = True
			if foundAssertion and (msgLine < 3):
				msgLine += 1
				msgAssertion += str(line)
			result = re.search('uci->stat', str(line))
			if result is not None and not exitSignalReceived:
				uciStatMsgCount += 1
			result = re.search('PDCP data request failed', str(line))
			if result is not None and not exitSignalReceived:
				pdcpDataReqFailedCount += 1
			result = re.search('bad DCI 1', str(line))
			if result is not None and not exitSignalReceived:
				badDciCount += 1
			result = re.search('Format1A Retransmission but TBS are different', str(line))
			if result is not None and not exitSignalReceived:
				f1aRetransmissionCount += 1
			result = re.search('FATAL ERROR', str(line))
			if result is not None and not exitSignalReceived:
				fatalErrorCount += 1
			result = re.search('MAC BSR Triggered ReTxBSR Timer expiry', str(line))
			if result is not None and not exitSignalReceived:
				macBsrTimerExpiredCount += 1
			result = re.search('Generating RRCConnectionReconfigurationComplete', str(line))
			if result is not None:
				rrcConnectionRecfgComplete += 1
			# No cell synchronization found, abandoning
			result = re.search('No cell synchronization found, abandoning', str(line))
			if result is not None:
				no_cell_sync_found = True
			if RAN.eNBmbmsEnables[0]:
				result = re.search('TRIED TO PUSH MBMS DATA', str(line))
				if result is not None:
					mbms_messages += 1
			result = re.search("MIB Information => ([a-zA-Z]{1,10}), ([a-zA-Z]{1,10}), NidCell (?P<nidcell>\d{1,3}), N_RB_DL (?P<n_rb_dl>\d{1,3}), PHICH DURATION (?P<phich_duration>\d), PHICH RESOURCE (?P<phich_resource>.{1,4}), TX_ANT (?P<tx_ant>\d)", str(line))
			if result is not None and (not mib_found):
				try:
					mibMsg = "MIB Information: " + result.group(1) + ', ' + result.group(2)
					HTML.htmlUEFailureMsg=HTML.htmlUEFailureMsg + mibMsg + '\n'
					logging.debug(f'\033[94m{mibMsg}\033[0m')
					mibMsg = "    nidcell = " + result.group('nidcell')
					HTML.htmlUEFailureMsg=HTML.htmlUEFailureMsg + mibMsg
					logging.debug(f'\033[94m{mibMsg}\033[0m')
					mibMsg = "    n_rb_dl = " + result.group('n_rb_dl')
					HTML.htmlUEFailureMsg=HTML.htmlUEFailureMsg + mibMsg + '\n'
					logging.debug(f'\033[94m{mibMsg}\033[0m')
					mibMsg = "    phich_duration = " + result.group('phich_duration')
					HTML.htmlUEFailureMsg=HTML.htmlUEFailureMsg + mibMsg
					logging.debug(f'\033[94m{mibMsg}\033[0m')
					mibMsg = "    phich_resource = " + result.group('phich_resource')
					HTML.htmlUEFailureMsg=HTML.htmlUEFailureMsg + mibMsg + '\n'
					logging.debug(f'\033[94m{mibMsg}\033[0m')
					mibMsg = "    tx_ant = " + result.group('tx_ant')
					HTML.htmlUEFailureMsg=HTML.htmlUEFailureMsg + mibMsg + '\n'
					logging.debug(f'\033[94m{mibMsg}\033[0m')
					mib_found = True
				except Exception as e:
					logging.error(f'\033[91m MIB marker was not found \033[0m')
			result = re.search("Initial sync: pbch decoded sucessfully", str(line))
			if result is not None and (not frequency_found):
				try:
					mibMsg = f"UE decoded PBCH successfully"
					HTML.htmlUEFailureMsg=HTML.htmlUEFailureMsg + mibMsg + '\n'
					logging.debug(f'\033[94m{mibMsg}\033[0m')
					frequency_found = True
				except Exception as e:
					logging.error(f'\033[91m UE did not find PBCH\033[0m')
			result = re.search("PLMN MCC (?P<mcc>\d{1,3}), MNC (?P<mnc>\d{1,3}), TAC", str(line))
			if result is not None and (not plmn_found):
				try:
					mibMsg = f"PLMN MCC = {result.group('mcc')} MNC = {result.group('mnc')}"
					HTML.htmlUEFailureMsg=HTML.htmlUEFailureMsg + mibMsg + '\n'
					logging.debug(f'\033[94m{mibMsg}\033[0m')
					plmn_found = True
				except Exception as e:
					logging.error(f'\033[91m PLMN not found \033[0m')
			result = re.search("Found (?P<operator>[\w,\s]{1,15}) \(name from internal table\)", str(line))
			if result is not None:
				try:
					mibMsg = f"The operator is: {result.group('operator')}"
					HTML.htmlUEFailureMsg=HTML.htmlUEFailureMsg + mibMsg + '\n'
					logging.debug(f'\033[94m{mibMsg}\033[0m')
				except Exception as e:
					logging.error(f'\033[91m Operator name not found \033[0m')
			result = re.search("SIB5 InterFreqCarrierFreq element (.{1,4})/(.{1,4})", str(line))
			if result is not None:
				try:
					mibMsg = f'SIB5 InterFreqCarrierFreq element {result.group(1)}/{result.group(2)}'
					HTML.htmlUEFailureMsg=HTML.htmlUEFailureMsg + mibMsg + ' -> '
					logging.debug(f'\033[94m{mibMsg}\033[0m')
				except Exception as e:
					logging.error(f'\033[91m SIB5 InterFreqCarrierFreq element not found \033[0m')
			result = re.search("DL Carrier Frequency/ARFCN : \-*(?P<carrier_frequency>\d{1,15}/\d{1,4})", str(line))
			if result is not None:
				try:
					freq = result.group('carrier_frequency')
					new_freq = re.sub('/[0-9]+','',freq)
					float_freq = float(new_freq) / 1000000
					HTML.htmlUEFailureMsg=HTML.htmlUEFailureMsg + 'DL Freq: ' + ('%.1f' % float_freq) + ' MHz'
					logging.debug(f'\033[94m    DL Carrier Frequency is:  {freq}\033[0m')
				except Exception as e:
					logging.error(f'\033[91m    DL Carrier Frequency not found \033[0m')
			result = re.search("AllowedMeasBandwidth : (?P<allowed_bandwidth>\d{1,7})", str(line))
			if result is not None:
				try:
					prb = result.group('allowed_bandwidth')
					HTML.htmlUEFailureMsg=HTML.htmlUEFailureMsg + ' -- PRB: ' + prb + '\n'
					logging.debug(f'\033[94m    AllowedMeasBandwidth: {prb}\033[0m')
				except Exception as e:
					logging.error(f'\033[91m    AllowedMeasBandwidth not found \033[0m')
		ue_log_file.close()
		if rrcConnectionRecfgComplete > 0:
			statMsg = f'UE connected to eNB ({rrcConnectionRecfgComplete}) RRCConnectionReconfigurationComplete message(s) generated)'
			logging.debug(f'\033[94m{statMsg}\033[0m')
			HTML.htmlUEFailureMsg=HTML.htmlUEFailureMsg + statMsg + '\n'
		if nrUEFlag:
			if nrDecodeMib > 0:
				statMsg = f'UE showed {nrDecodeMib} "MIB decode" message(s)'
				logging.debug(f'\u001B[1;30;43m{statMsg}\u001B[0m')
				HTML.htmlUEFailureMsg=HTML.htmlUEFailureMsg + statMsg + '\n'
			if nrFoundDCI > 0:
				statMsg = f'UE showed {nrFoundDCI} "DCI found" message(s)'
				logging.debug(f'\u001B[1;30;43m{statMsg}\u001B[0m')
				HTML.htmlUEFailureMsg=HTML.htmlUEFailureMsg + statMsg + '\n'
			if nrCRCOK > 0:
				statMsg = f'UE showed {nrCRCOK} "PDSCH decoding" message(s)'
				logging.debug(f'\u001B[1;30;43m{statMsg}\u001B[0m')
				HTML.htmlUEFailureMsg=HTML.htmlUEFailureMsg + statMsg + '\n'
			if not frequency_found:
				statMsg = 'NR-UE could NOT synch!'
				logging.error(f'\u001B[1;30;43m{statMsg}\u001B[0m')
				HTML.htmlUEFailureMsg=HTML.htmlUEFailureMsg + statMsg + '\n'
			if nbPduSessAccept > 0:
				statMsg = f'UE showed {nbPduSessAccept} "Received PDU Session Establishment Accept" message(s)'
				logging.debug(f'\u001B[1;30;43m{statMsg}\u001B[0m')
				HTML.htmlUEFailureMsg=HTML.htmlUEFailureMsg + statMsg + '\n'
			if nbPduDiscard > 0:
				statMsg = f'UE showed {nbPduDiscard} "warning: discard PDU, sn out of window" message(s)'
				logging.debug(f'\u001B[1;30;43m{statMsg}\u001B[0m')
				HTML.htmlUEFailureMsg=HTML.htmlUEFailureMsg + statMsg + '\n'
		if uciStatMsgCount > 0:
			statMsg = f'UE showed {uciStatMsgCount} "uci->stat" message(s)'
			logging.debug(f'\u001B[1;30;43m{statMsg}\u001B[0m')
			HTML.htmlUEFailureMsg=HTML.htmlUEFailureMsg + statMsg + '\n'
		if pdcpDataReqFailedCount > 0:
			statMsg = f'UE showed {pdcpDataReqFailedCount} "PDCP data request failed" message(s)'
			logging.debug(f'\u001B[1;30;43m{statMsg}\u001B[0m')
			HTML.htmlUEFailureMsg=HTML.htmlUEFailureMsg + statMsg + '\n'
		if badDciCount > 0:
			statMsg = f'UE showed {badDciCount} "bad DCI 1(A)" message(s)'
			logging.debug(f'\u001B[1;30;43m{statMsg}\u001B[0m')
			HTML.htmlUEFailureMsg=HTML.htmlUEFailureMsg + statMsg + '\n'
		if f1aRetransmissionCount > 0:
			statMsg = f'UE showed {f1aRetransmissionCount} "Format1A Retransmission but TBS are different" message(s)'
			logging.debug(f'\u001B[1;30;43m{statMsg}\u001B[0m')
			HTML.htmlUEFailureMsg=HTML.htmlUEFailureMsg + statMsg + '\n'
		if fatalErrorCount > 0:
			statMsg = f'UE showed {fatalErrorCount} "FATAL ERROR:" message(s)'
			logging.debug(f'\u001B[1;30;43m{statMsg}\u001B[0m')
			HTML.htmlUEFailureMsg=HTML.htmlUEFailureMsg + statMsg + '\n'
		if macBsrTimerExpiredCount > 0:
			statMsg = f'UE showed {fatalErrorCount} "MAC BSR Triggered ReTxBSR Timer expiry" message(s)'
			logging.debug(f'\u001B[1;30;43m{statMsg}\u001B[0m')
			HTML.htmlUEFailureMsg=HTML.htmlUEFailureMsg + statMsg + '\n'
		if RAN.eNBmbmsEnables[0]:
			if mbms_messages > 0:
				statMsg = f'UE showed {mbms_messages} "TRIED TO PUSH MBMS DATA" message(s)'
				logging.debug(f'\u001B[1;30;43m{statMsg}\u001B[0m')
			else:
				statMsg = 'UE did NOT SHOW "TRIED TO PUSH MBMS DATA" message(s)'
				logging.debug(f'\u001B[1;30;41m{statMsg}\u001B[0m')
				global_status = CONST.OAI_UE_PROCESS_NO_MBMS_MSGS
			HTML.htmlUEFailureMsg=HTML.htmlUEFailureMsg + statMsg + '\n'
		if foundSegFault:
			logging.debug('\u001B[1;37;41m UE ended with a Segmentation Fault! \u001B[0m')
			if not nrUEFlag:
				global_status = CONST.OAI_UE_PROCESS_SEG_FAULT
			else:
				if not frequency_found:
					global_status = CONST.OAI_UE_PROCESS_SEG_FAULT
		if foundAssertion:
			logging.debug('\u001B[1;30;43m UE showed an assertion! \u001B[0m')
			HTML.htmlUEFailureMsg=HTML.htmlUEFailureMsg + 'UE showed an assertion!\n'
			if not nrUEFlag:
				if not mib_found or not frequency_found:
					global_status = CONST.OAI_UE_PROCESS_ASSERTION
			else:
				if not frequency_found:
					global_status = CONST.OAI_UE_PROCESS_ASSERTION
		if foundRealTimeIssue:
			logging.debug('\u001B[1;37;41m UE faced real time issues! \u001B[0m')
			HTML.htmlUEFailureMsg=HTML.htmlUEFailureMsg + 'UE faced real time issues!\n'
		if nrUEFlag:
			if not frequency_found:
				global_status = CONST.OAI_UE_PROCESS_COULD_NOT_SYNC
		else:
			if no_cell_sync_found and not mib_found:
				logging.debug('\u001B[1;37;41m UE could not synchronize ! \u001B[0m')
				HTML.htmlUEFailureMsg=HTML.htmlUEFailureMsg + 'UE could not synchronize!\n'
				global_status = CONST.OAI_UE_PROCESS_COULD_NOT_SYNC
		return global_status

	def TerminateUE(self, HTML):
		ues = [cls_module.Module_UE(n.strip()) for n in self.ue_ids]
		with concurrent.futures.ThreadPoolExecutor() as executor:
			futures = [executor.submit(ue.terminate) for ue in ues]
			archives = [f.result() for f in futures]
		archive_info = [f'Log at: {a}' if a else 'No log available' for a in archives]
		messages = [f"UE {ue.getName()}: {log}" for (ue, log) in zip(ues, archive_info)]
		HTML.CreateHtmlTestRowQueue(f'N/A', 'OK', messages)

	def TerminateOAIUE(self,HTML,RAN,EPC,CONTAINERS):
		SSH = sshconnection.SSHConnection()
		SSH.open(self.UEIPAddress, self.UEUserName, self.UEPassword)
		SSH.command(f'cd {self.UESourceCodePath}/cmake_targets', '\$', 5)
		SSH.command('ps -aux | grep --color=never softmodem | grep -v grep', '\$', 5)
		result = re.search('-uesoftmodem', SSH.getBefore())
		if result is not None:
			SSH.command(f'echo {self.UEPassword} | sudo -S killall --signal SIGINT -r .*-uesoftmodem || true', '\$', 5)
			time.sleep(10)
			SSH.command('ps -aux | grep --color=never softmodem | grep -v grep', '\$', 5)
			result = re.search('-uesoftmodem', SSH.getBefore())
			if result is not None:
				SSH.command(f'echo {self.UEPassword} | sudo -S killall --signal SIGKILL -r .*-uesoftmodem || true', '\$', 5)
				time.sleep(5)
		SSH.command(f'rm -f my-lte-uesoftmodem-run {self.UE_instance}.sh', '\$', 5)
		SSH.close()
		result = re.search('ue_', str(self.UELogFile))
		if result is not None:
			copyin_res = SSH.copyin(self.UEIPAddress, self.UEUserName, self.UEPassword,f'{self.UESourceCodePath}/cmake_targets/{self.UELogFile}', '.')
			if (copyin_res == -1):
				logging.debug('\u001B[1;37;41m Could not copy UE logfile to analyze it! \u001B[0m')
				HTML.htmlUEFailureMsg='Could not copy UE logfile to analyze it!'
				HTML.CreateHtmlTestRow('N/A', 'KO', CONST.OAI_UE_PROCESS_NOLOGFILE_TO_ANALYZE, 'UE')
				self.UELogFile = ''
				return
			logging.debug('\u001B[1m Analyzing UE logfile \u001B[0m')
			logStatus = self.AnalyzeLogFile_UE(self.UELogFile,HTML,RAN)
			result = re.search('--no-L2-connect', str(self.Initialize_OAI_UE_args))
			if result is not None:
				ueAction = 'Sniffing'
			else:
				ueAction = 'Connection'
			if (logStatus < 0):
				logging.debug(f'\u001B[1m {ueAction} Failed \u001B[0m')
				HTML.htmlUEFailureMsg='<b>' + ueAction + ' Failed</b>\n' + HTML.htmlUEFailureMsg
				HTML.CreateHtmlTestRow('N/A', 'KO', logStatus, 'UE')
				if self.air_interface == 'lte-uesoftmodem':
					# In case of sniffing on commercial eNBs we have random results
					# Not an error then
					if (logStatus != CONST.OAI_UE_PROCESS_COULD_NOT_SYNC) or (ueAction != 'Sniffing'):
						self.Initialize_OAI_UE_args = ''
						self.AutoTerminateUEandeNB(HTML,RAN,EPC,CONTAINERS)
				else:
					if (logStatus == CONST.OAI_UE_PROCESS_COULD_NOT_SYNC):
						self.Initialize_OAI_UE_args = ''
						self.AutoTerminateUEandeNB(HTML,RAN,EPC,CONTAINERS)
			else:
				logging.debug(f'\u001B[1m {ueAction} Completed \u001B[0m')
				HTML.htmlUEFailureMsg='<b>' + ueAction + ' Completed</b>\n' + HTML.htmlUEFailureMsg
				HTML.CreateHtmlTestRow('N/A', 'OK', CONST.ALL_PROCESSES_OK)
			self.UELogFile = ''
		else:
			HTML.CreateHtmlTestRow('N/A', 'OK', CONST.ALL_PROCESSES_OK)

	def AutoTerminateUEandeNB(self,HTML,RAN,EPC,CONTAINERS):
		# TODO: terminate UE?
		if (self.Initialize_OAI_UE_args != ''):
			self.testCase_id = 'AUTO-KILL-OAI-UE'
			HTML.testCase_id = self.testCase_id
			self.desc = 'Automatic Termination of OAI-UE'
			HTML.desc = self.desc
			self.ShowTestID()
			self.TerminateOAIUE(HTML,RAN,EPC,CONTAINERS)
		if (RAN.Initialize_eNB_args != ''):
			self.testCase_id = 'AUTO-KILL-RAN'
			HTML.testCase_id = self.testCase_id
			self.desc = 'Automatic Termination of all RAN nodes'
			HTML.desc = self.desc
			self.ShowTestID()
			#terminate all RAN nodes eNB/gNB/OCP
			for instance in range(0, len(RAN.air_interface)):
				if RAN.air_interface[instance]!='':
					logging.debug(f'Auto Termination of Instance {instance} : {RAN.air_interface[instance]}')
					RAN.eNB_instance=instance
					RAN.TerminateeNB(HTML,EPC)
		if CONTAINERS.yamlPath[0] != '':
			self.testCase_id = 'AUTO-KILL-CONTAINERS'
			HTML.testCase_id = self.testCase_id
			self.desc = 'Automatic Termination of all RAN containers'
			HTML.desc = self.desc
			self.ShowTestID()
			for instance in range(0, len(CONTAINERS.yamlPath)):
				if CONTAINERS.yamlPath[instance]!='':
					CONTAINERS.eNB_instance=instance
					if CONTAINERS.deployKind[instance]:
						CONTAINERS.UndeployObject(HTML,RAN)
					else:
						CONTAINERS.UndeployGenObject(HTML,RAN, self)
		RAN.prematureExit=True

	#this function is called only if eNB/gNB fails to start
	#RH to be re-factored
	def AutoTerminateeNB(self,HTML,RAN,EPC,CONTAINERS):
		if (RAN.Initialize_eNB_args != ''):
			self.testCase_id = 'AUTO-KILL-RAN'
			HTML.testCase_id = self.testCase_id
			self.desc = 'Automatic Termination of all RAN nodes'
			HTML.desc = self.desc
			self.ShowTestID()
			#terminate all RAN nodes eNB/gNB/OCP
			for instance in range(0, len(RAN.air_interface)):
				if RAN.air_interface[instance]!='':
					logging.debug(f'Auto Termination of Instance {instance} : {RAN.air_interface[instance]}')
					RAN.eNB_instance=instance
					RAN.TerminateeNB(HTML,EPC)
		if CONTAINERS.yamlPath[0] != '':
			self.testCase_id = 'AUTO-KILL-CONTAINERS'
			HTML.testCase_id = self.testCase_id
			self.desc = 'Automatic Termination of all RAN containers'
			HTML.desc = self.desc
			self.ShowTestID()
			for instance in range(0, len(CONTAINERS.yamlPath)):
				if CONTAINERS.yamlPath[instance]!='':
					CONTAINERS.eNB_instance=instance
					if CONTAINERS.deployKind[instance]:
						CONTAINERS.UndeployObject(HTML,RAN)
					else:
						CONTAINERS.UndeployGenObject(HTML,RAN,self)
		RAN.prematureExit=True

	def IdleSleep(self,HTML):
		time.sleep(self.idle_sleep_time)
		HTML.CreateHtmlTestRow(str(self.idle_sleep_time) + ' sec', 'OK', CONST.ALL_PROCESSES_OK)

	def X2_Status(self, idx, fileName, EPC):
		cmd = "curl --silent http://" + EPC.IPAddress + ":9999/stats | jq '.' > " + fileName
		message = cmd + '\n'
		logging.debug(cmd)
		subprocess.run(cmd, shell=True)
		if idx == 0:
			cmd = "jq '.mac_stats | length' " + fileName
			strNbEnbs = subprocess.check_output(cmd, shell=True, universal_newlines=True)
			self.x2NbENBs = int(strNbEnbs.strip())
		cnt = 0
		while cnt < self.x2NbENBs:
			cmd = "jq '.mac_stats[" + str(cnt) + "].bs_id' " + fileName
			bs_id = subprocess.check_output(cmd, shell=True, universal_newlines=True)
			self.x2ENBBsIds[idx].append(bs_id.strip())
			cmd = "jq '.mac_stats[" + str(cnt) + "].ue_mac_stats | length' " + fileName
			stNbUEs = subprocess.check_output(cmd, shell=True, universal_newlines=True)
			nbUEs = int(stNbUEs.strip())
			ueIdx = 0
			self.x2ENBConnectedUEs[idx].append([])
			while ueIdx < nbUEs:
				cmd = "jq '.mac_stats[" + str(cnt) + "].ue_mac_stats[" + str(ueIdx) + "].rnti' " + fileName
				rnti = subprocess.check_output(cmd, shell=True, universal_newlines=True)
				self.x2ENBConnectedUEs[idx][cnt].append(rnti.strip())
				ueIdx += 1
			cnt += 1

		cnt = 0
		while cnt < self.x2NbENBs:
			msg = "   -- eNB: " + str(self.x2ENBBsIds[idx][cnt]) + " is connected to " + str(len(self.x2ENBConnectedUEs[idx][cnt])) + " UE(s)"
			logging.debug(msg)
			message += msg + '\n'
			ueIdx = 0
			while ueIdx < len(self.x2ENBConnectedUEs[idx][cnt]):
				msg = "      -- UE rnti: " + str(self.x2ENBConnectedUEs[idx][cnt][ueIdx])
				logging.debug(msg)
				message += msg + '\n'
				ueIdx += 1
			cnt += 1
		return message

	def Perform_X2_Handover(self,HTML,RAN,EPC):
		html_queue = SimpleQueue()
		fullMessage = '<pre style="background-color:white">'
		msg = f'Doing X2 Handover w/ option {self.x2_ho_options}'
		logging.debug(msg)
		fullMessage += msg + '\n'
		if self.x2_ho_options == 'network':
			HTML.CreateHtmlTestRow('Cannot perform requested X2 Handover', 'KO', CONST.ALL_PROCESSES_OK)

	def LogCollectBuild(self,RAN):
		# Some pipelines are using "none" IP / Credentials
		# In that case, just forget about it
		if RAN.eNBIPAddress == 'none' or self.UEIPAddress == 'none':
			sys.exit(0)

		if (RAN.eNBIPAddress != '' and RAN.eNBUserName != '' and RAN.eNBPassword != ''):
			IPAddress = RAN.eNBIPAddress
			UserName = RAN.eNBUserName
			Password = RAN.eNBPassword
			SourceCodePath = RAN.eNBSourceCodePath
		elif (self.UEIPAddress != '' and self.UEUserName != '' and self.UEPassword != ''):
			IPAddress = self.UEIPAddress
			UserName = self.UEUserName
			Password = self.UEPassword
			SourceCodePath = self.UESourceCodePath
		else:
			sys.exit('Insufficient Parameter')
		SSH = sshconnection.SSHConnection()
		SSH.open(IPAddress, UserName, Password)
		SSH.command(f'cd {SourceCodePath}', '\$', 5)
		SSH.command('cd cmake_targets', '\$', 5)
		SSH.command('rm -f build.log.zip', '\$', 5)
		SSH.command('zip -r build.log.zip build_log_*/*', '\$', 60)
		SSH.close()

	def LogCollectPing(self,EPC):
		# Some pipelines are using "none" IP / Credentials
		# In that case, just forget about it
		if EPC.IPAddress == 'none':
			sys.exit(0)
		SSH = sshconnection.SSHConnection()
		SSH.open(EPC.IPAddress, EPC.UserName, EPC.Password)
		SSH.command(f'cd {EPC.SourceCodePath}', '\$', 5)
		SSH.command('cd scripts', '\$', 5)
		SSH.command('rm -f ping.log.zip', '\$', 5)
		SSH.command('zip ping.log.zip ping*.log', '\$', 60)
		SSH.command('rm ping*.log', '\$', 5)
		SSH.close()

	def LogCollectIperf(self,EPC):
		# Some pipelines are using "none" IP / Credentials
		# In that case, just forget about it
		if EPC.IPAddress == 'none':
			sys.exit(0)
		SSH = sshconnection.SSHConnection()
		SSH.open(EPC.IPAddress, EPC.UserName, EPC.Password)
		SSH.command(f'cd {EPC.SourceCodePath}', '\$', 5)
		SSH.command('cd scripts', '\$', 5)
		SSH.command('rm -f iperf.log.zip', '\$', 5)
		SSH.command('zip iperf.log.zip iperf*.log', '\$', 60)
		SSH.command('rm iperf*.log', '\$', 5)
		SSH.close()
	
	def LogCollectOAIUE(self):
		# Some pipelines are using "none" IP / Credentials
		# In that case, just forget about it
		if self.UEIPAddress == 'none':
			sys.exit(0)
		SSH = sshconnection.SSHConnection()
		SSH.open(self.UEIPAddress, self.UEUserName, self.UEPassword)
		SSH.command(f'cd {self.UESourceCodePath}', '\$', 5)
		SSH.command(f'cd cmake_targets', '\$', 5)
		SSH.command(f'echo {self.UEPassword} | sudo -S rm -f ue.log.zip', '\$', 5)
		SSH.command(f'echo {self.UEPassword} | sudo -S zip ue.log.zip ue*.log core* ue_*record.raw ue_*.pcap ue_*txt', '\$', 60)
		SSH.command(f'echo {self.UEPassword} | sudo -S rm ue*.log core* ue_*record.raw ue_*.pcap ue_*txt', '\$', 5)
		SSH.close()

	def ConditionalExit(self):
		if self.testUnstable:
			if self.testStabilityPointReached or self.testMinStableId == '999999':
				sys.exit(0)
		sys.exit(1)

	def ShowTestID(self):
		logging.info(f'\u001B[1m----------------------------------------\u001B[0m')
		logging.info(f'\u001B[1m Test ID: {self.testCase_id} \u001B[0m')
		logging.info(f'\u001B[1m {self.desc} \u001B[0m')
		logging.info(f'\u001B[1m----------------------------------------\u001B[0m')
