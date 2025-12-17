#!/usr/bin/env groovy

// This pipeline is designed to run on Esri-internal CI infrastructure.

import groovy.transform.Field

@Library('psl')
import com.esri.zrh.jenkins.PipelineSupportLibrary as PSL
import com.esri.zrh.jenkins.JenkinsTools
import com.esri.zrh.jenkins.ce.CityEnginePipelineLibrary as CEPL
import com.esri.zrh.jenkins.ce.PrtAppPipelineLibrary as PAPL
import com.esri.zrh.jenkins.PslFactory
import com.esri.zrh.jenkins.psl.UploadTrackingPsl

@Field def psl = PslFactory.create(this, UploadTrackingPsl.ID)
@Field def cepl = new CEPL(this, psl)
@Field def papl = new PAPL(cepl)


// -- GLOBAL DEFINITIONS

@Field final String REPO         = 'https://github.com/Esri/palladio.git'
@Field final String SOURCE       = "palladio.git/src"
@Field final String BUILD_TARGET = 'package'
@Field final String SOURCE_STASH = 'palladio-src'

@Field final String DOCKER_IMAGE_REV = 'v2'
@Field final String DOCKER_IMAGE_ID = 'palladio/palladio-tc'
@Field final String DOCKER_IMAGE_TAG_BASE_LINUX = 'almalinux8-gcc11'
@Field final String DOCKER_IMAGE_TAG_BASE_WINDOWS = 'win19-vc1438'

@Field final String[] HDK_VER = [ '21.0.559', '20.5.684', '20.0.896' ]

@Field final List CONFIGS_CHECKOUT = [ [ ba: PSL.BA_CHECKOUT ] ]
@Field final Map DOCKER_IMAGE_LINUX_CONFIG = [ ba: PSL.BA_LINUX_DOCKER, containerWorkspace: "/tmp/work" ]
@Field final Map DOCKER_IMAGE_WINDOWS_CONFIG = [ ba: 'win19-64-d', containerWorkspace: 'c:/tmp/work' ]
@Field final Map BUILD_ENV_LINUX_CONFIG = [ os: CEPL.CFG_OS_RHEL8, bc: CEPL.CFG_BC_REL, tc: CEPL.CFG_TC_GCC112, cc: CEPL.CFG_CC_OPT, arch: CEPL.CFG_ARCH_X86_64 ]
@Field final Map BUILD_ENV_WINDOWS_CONFIG = [ os: CEPL.CFG_OS_WIN10, bc: CEPL.CFG_BC_REL, tc: CEPL.CFG_TC_VC1438, cc: CEPL.CFG_CC_OPT, arch: CEPL.CFG_ARCH_X86_64 ]
@Field final Map BUILD_ENV_LINUX_CONFIG_CESDK_LATEST = BUILD_ENV_LINUX_CONFIG
@Field final Map BUILD_ENV_WINDOWS_CONFIG_CESDK_LATEST = BUILD_ENV_WINDOWS_CONFIG + [ tc: CEPL.CFG_TC_VC1438 ]
@Field final Map BUILD_ENV_CESDK_LATEST = [ grp: 'cesdkLatest', cesdk: PAPL.Dependencies.CESDK_LATEST ]

@Field final List CONFIGS_TEST = [
	DOCKER_IMAGE_LINUX_CONFIG + BUILD_ENV_LINUX_CONFIG + [ houdini: HDK_VER[0] ],
	DOCKER_IMAGE_WINDOWS_CONFIG + BUILD_ENV_WINDOWS_CONFIG + [ houdini: HDK_VER[0] ],
]


// -- SETUP

psl.runsHere('production')
env.PIPELINE_ARCHIVING_ALLOWED = "true"
properties([ disableConcurrentBuilds() ])


// -- PIPELINE

stage('prepare') {
	cepl.runParallel(taskGenCheckout())
}

stage('test') {
	cepl.runParallel(taskGenTest())
}

stage('build') {
	cepl.runParallel(taskGenBuild())
}

papl.finalizeRun('palladio', env.BRANCH_NAME)


// -- TASK GENERATORS

Map taskGenCheckout(){
	Map tasks = [:]
	tasks << cepl.generateTasks('pld-src', this.&taskSourceCheckout, CONFIGS_CHECKOUT)
	return tasks
}

Map taskGenTest() {
    Map tasks = [:]
	tasks << cepl.generateTasks('pld-test', this.&taskRunTest, CONFIGS_TEST)
	return tasks;
}

Map taskGenBuild() {
	Map tasks = [:]
	for (int i = 0; i < HDK_VER.length; i++) {
		Map hdk = [ houdini: HDK_VER[i] ]
		List config = [
			DOCKER_IMAGE_LINUX_CONFIG + BUILD_ENV_LINUX_CONFIG + hdk,
			DOCKER_IMAGE_WINDOWS_CONFIG + BUILD_ENV_WINDOWS_CONFIG + hdk,
		]

		// for latest HDK, we also build against the internal latest CESDK build
		if (i == 0) {
			config << DOCKER_IMAGE_LINUX_CONFIG + BUILD_ENV_LINUX_CONFIG_CESDK_LATEST + BUILD_ENV_CESDK_LATEST + hdk
			config << DOCKER_IMAGE_WINDOWS_CONFIG + BUILD_ENV_WINDOWS_CONFIG_CESDK_LATEST + BUILD_ENV_CESDK_LATEST + hdk
		}

		tasks << cepl.generateTasks('pld-hdk'+i, this.&taskBuildPalladio, config)
	}
	return tasks;
}


// -- TASK BUILDERS

def taskSourceCheckout(cfg) {
	cepl.cleanCurrentDir()
	papl.checkout(REPO, env.BRANCH_NAME)
	stash(name: SOURCE_STASH)
}

def taskRunTest(cfg) {
	cepl.cleanCurrentDir()
	unstash(name: SOURCE_STASH)
	buildPalladio(cfg, [], 'build_and_run_tests')
	junit('build/test/palladio_test_report.xml')
}

def taskBuildPalladio(cfg) {
	cepl.cleanCurrentDir()
	unstash(name: SOURCE_STASH)

	List defs = applyCeSdkOverride(cfg) + [
		[ key: 'HOUDINI_USER_PATH',   val: "${env.WORKSPACE}/install" ],
		[ key: 'PLD_VERSION_BUILD',   val: env.BUILD_NUMBER ],
		[ key: 'PLD_HOUDINI_VERSION', val: cfg.houdini]
	]

	buildPalladio(cfg, defs, BUILD_TARGET)

	def versionExtractor = { p ->
		def vers = (p =~ /.*palladio-(.*)\.hdn.*/)
		return vers[0][1]
	}
	def classifierExtractor = { p ->
		def cls = (p =~ /.*palladio-.*\.(hdn.*)-(windows|linux)\..*/)
		return cls[0][1] + '.' + cepl.getArchiveClassifier(cfg)
	}
	papl.publish('palladio', env.BRANCH_NAME, "palladio-*", versionExtractor, cfg, classifierExtractor)
}

def buildPalladio(cfg, defs, target) {
	final Map dirMap = [ "${env.WORKSPACE}" : cfg.containerWorkspace ]
	final String src = "${cfg.containerWorkspace}/${SOURCE}";
	final String bld = "${cfg.containerWorkspace}/build";
	final Map envMap = [ WORKSPACE: cfg.containerWorkspace ]

	String cmd = ''
	if (cfg.os == CEPL.CFG_OS_RHEL8) {
		envMap << [ DEFAULT_UID: '$(id -u)', DEFAULT_GID: '$(id -g)' ]
	}

    cmd += "cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -S ${src} -B ${bld}"
    defs.each { d ->
        String val = (d.val instanceof Closure) ? d.val.call(cfg) : d.val
        val = val.replace('%', '%%') // Windows: defer expansion of env vars to container run time
        cmd += " -D${d.key}=${val}"
    }
	cmd += " && cmake --build ${bld} --target ${target}"
	psl.runDockerCmd(getContainerId(cfg), cfg.containerWorkspace, cmd, dirMap, envMap)
}

String getContainerId(cfg) {
	String tagBase = (cfg.os == CEPL.CFG_OS_RHEL8) ? DOCKER_IMAGE_TAG_BASE_LINUX : DOCKER_IMAGE_TAG_BASE_WINDOWS
	String hdkVersion = cfg.houdini.replace(".", "")
	return "${DOCKER_IMAGE_ID}:${tagBase}-hdk${hdkVersion}-${DOCKER_IMAGE_REV}"
}

def scanAndPublishBuildIssues(Map cfg, String consoleOut) {
	final String houdiniSuf = cfg.houdini.replace('.', '_')
	final String buildSuf = "${cepl.prtBuildSuffix(cfg)}-${houdiniSuf}"
	final String buildLog = "build-${buildSuf}.log"
	final String idSuf = (cfg.grp ? cfg.grp + "-" : "") + "${houdiniSuf}-${cepl.getArchiveClassifier(cfg)}"

	// dump build log to file for warnings scanner
	writeFile(file: buildLog, text: consoleOut)

	// scan for compiler warnings
	def scanReport = scanForIssues(tool: cepl.isGCC(cfg) ? gcc4(pattern: buildLog) : msBuild(pattern: buildLog), blameDisabled: true)
	publishIssues(id: "palladio-warnings-${idSuf}", name: "palladio-${idSuf}", issues: [scanReport])
}

List applyCeSdkOverride(cfg) {
	if (cfg.cesdk) {
		papl.fetchDependency(cfg.cesdk, cfg)
		return [ [ key: 'PLD_CESDK_DIR:PATH', val: cfg.cesdk.p ] ]
	}
	return []
}
