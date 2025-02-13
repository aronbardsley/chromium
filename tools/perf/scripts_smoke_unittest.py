# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

from telemetry import decorators
from telemetry.testing import options_for_unittests

RUNNER_SCRIPTS_DIR = os.path.join(os.path.dirname(__file__),
                                  '..', '..', 'testing', 'scripts')
sys.path.append(RUNNER_SCRIPTS_DIR)
import run_performance_tests  # pylint: disable=wrong-import-position,import-error


class ScriptsSmokeTest(unittest.TestCase):

  perf_dir = os.path.dirname(__file__)

  def RunPerfScript(self, command, env=None):
    main_command = [sys.executable]
    args = main_command + command.split(' ')
    proc = subprocess.Popen(args, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, cwd=self.perf_dir,
                            env=env)
    stdout = proc.communicate()[0]
    return_code = proc.returncode
    return return_code, stdout

  def testRunBenchmarkHelp(self):
    return_code, stdout = self.RunPerfScript('run_benchmark help')
    self.assertEquals(return_code, 0, stdout)
    self.assertIn('Available commands are', stdout)

  def testRunBenchmarkRunListsOutBenchmarks(self):
    return_code, stdout = self.RunPerfScript('run_benchmark run')
    self.assertIn('Pass --browser to list benchmarks', stdout)
    self.assertNotEquals(return_code, 0)

  def testRunBenchmarkRunNonExistingBenchmark(self):
    return_code, stdout = self.RunPerfScript('run_benchmark foo')
    self.assertIn('No benchmark named "foo"', stdout)
    self.assertNotEquals(return_code, 0)

  def testRunRecordWprHelp(self):
    return_code, stdout = self.RunPerfScript('record_wpr')
    self.assertEquals(return_code, 0, stdout)
    self.assertIn('optional arguments:', stdout)

  @decorators.Disabled('chromeos')  # crbug.com/814068
  def testRunRecordWprList(self):
    return_code, stdout = self.RunPerfScript('record_wpr --list-benchmarks')
    # TODO(nednguyen): Remove this once we figure out why importing
    # small_profile_extender fails on Android dbg.
    # crbug.com/561668
    if 'ImportError: cannot import name small_profile_extender' in stdout:
      self.skipTest('small_profile_extender is missing')
    self.assertEquals(return_code, 0, stdout)
    self.assertIn('kraken', stdout)

  @decorators.Disabled('chromeos')  # crbug.com/754913
  def testRunPerformanceTestsTelemetry_end2end(self):
    options = options_for_unittests.GetCopy()
    browser_type = options.browser_type
    tempdir = tempfile.mkdtemp()
    benchmark = 'dummy_benchmark.stable_benchmark_1'
    return_code, stdout = self.RunPerfScript(
        '../../testing/scripts/run_performance_tests.py '
        '../../tools/perf/run_benchmark '
        '--benchmarks=dummy_benchmark.stable_benchmark_1 '
        '--browser=%s '
        '--isolated-script-test-repeat=2 '
        '--isolated-script-test-also-run-disabled-tests '
        '--isolated-script-test-output=%s' % (
            browser_type,
            os.path.join(tempdir, 'output.json')
        ))
    self.assertEquals(return_code, 0, stdout)
    try:
      # By design, run_performance_tests.py does not output test results
      # to the location passed in by --isolated-script-test-output. Instead
      # it uses that directory of that file and puts stuff in its own
      # subdirectories for the purposes of merging later.
      with open(os.path.join(tempdir, benchmark, 'test_results.json')) as f:
        test_results = json.load(f)
        self.assertIsNotNone(
            test_results, 'json_test_results should be populated: ' + stdout)
        test_repeats = test_results['num_failures_by_type']['PASS']
        self.assertEqual(
            test_repeats, 2, '--isolated-script-test-repeat=2 should work.')
      with open(os.path.join(tempdir, benchmark, 'perf_results.json')) as f:
        perf_results = json.load(f)
        self.assertIsNotNone(
            perf_results, 'json perf results should be populated: ' + stdout)
    except IOError as e:
      self.fail('json_test_results should be populated: ' + stdout + str(e))
    finally:
      shutil.rmtree(tempdir)

  # Android: crbug.com/932301
  # ChromeOS: crbug.com/754913
  @decorators.Disabled('chromeos', 'android')
  def testRunPerformanceTestsTelemetrySharded_end2end(self):
    options = options_for_unittests.GetCopy()
    browser_type = options.browser_type
    tempdir = tempfile.mkdtemp()
    env = os.environ.copy()
    env['GTEST_SHARD_INDEX'] = '0'
    env['GTEST_TOTAL_SHARDS'] = '2'
    return_code, stdout = self.RunPerfScript(
        '../../testing/scripts/run_performance_tests.py '
        '../../tools/perf/run_benchmark '
        '--test-shard-map-filename=smoke_test_benchmark_shard_map.json '
        '--browser=%s '
        '--run-ref-build '
        '--isolated-script-test-repeat=2 '
        '--isolated-script-test-also-run-disabled-tests '
        '--isolated-script-test-output=%s' % (
            browser_type,
            os.path.join(tempdir, 'output.json')
        ), env=env)
    self.assertEquals(return_code, 0, stdout)
    try:
      expected_benchmark_folders = (
          'dummy_benchmark.stable_benchmark_1',
          'dummy_benchmark.stable_benchmark_1.reference')
      for folder in expected_benchmark_folders:
        with open(os.path.join(tempdir, folder, 'test_results.json')) as f:
          test_results = json.load(f)
          self.assertIsNotNone(
              test_results, 'json test results should be populated: ' + stdout)
          test_repeats = test_results['num_failures_by_type']['PASS']
          self.assertEqual(
              test_repeats, 2, '--isolated-script-test-repeat=2 should work.')
        with open(os.path.join(tempdir, folder, 'perf_results.json')) as f:
          perf_results = json.load(f)
          self.assertIsNotNone(
              perf_results, 'json perf results should be populated: ' + stdout)
    except IOError as e:
      self.fail('IOError: ' + stdout + str(e))
    except KeyError as e:
      self.fail('KeyError: ' + stdout + str(e))
    finally:
      shutil.rmtree(tempdir)


  # Windows: ".exe" is auto-added which breaks Windows.
  # ChromeOS: crbug.com/754913.
  @decorators.Disabled('win', 'chromeos')
  def testRunPerformanceTestsGtest_end2end(self):
    tempdir = tempfile.mkdtemp()
    benchmark = 'dummy_gtest'
    return_code, stdout = self.RunPerfScript(
        '../../testing/scripts/run_performance_tests.py ' +
        os.path.join('..', '..', 'tools', 'perf', 'testdata', 'dummy_gtest') +
        ' --non-telemetry=true '
        '--this-arg=passthrough '
        '--gtest-benchmark-name dummy_gtest '
        '--isolated-script-test-output=/x/y/z/output.json '
        '--isolated-script-test-output=%s' % (
            os.path.join(tempdir, 'output.json')
        ))
    self.assertEquals(return_code, 0, stdout)
    try:
      # By design, run_performance_tests.py does not output test results
      # to the location passed in by --isolated-script-test-output. Instead
      # it uses the directory of that file and puts stuff in its own
      # subdirectories for the purposes of merging later.
      with open(os.path.join(tempdir, benchmark, 'test_results.json')) as f:
        test_results = json.load(f)
        self.assertIsNotNone(
            test_results, 'json_test_results should be populated: ' + stdout)
      with open(os.path.join(tempdir, benchmark, 'perf_results.json')) as f:
        perf_results = json.load(f)
        self.assertIsNotNone(
            perf_results, 'json perf results should be populated: ' + stdout)
    except IOError as e:
      self.fail('json_test_results should be populated: ' + stdout + str(e))
    finally:
      shutil.rmtree(tempdir)

  def testRunPerformanceTestsTelemetryArgsParser(self):
    options = run_performance_tests.parse_arguments([
        '../../tools/perf/run_benchmark', '-v', '--browser=release_x64',
        '--upload-results', '--run-ref-build',
        '--test-shard-map-filename=win-10-perf_map.json',
        '--assert-gpu-compositing',
        r'--isolated-script-test-output=c:\a\b\c\output.json',
        r'--isolated-script-test-perf-output=c:\a\b\c\perftest-output.json',
        '--passthrough-arg=--a=b',
    ])
    self.assertIn('--assert-gpu-compositing', options.passthrough_args)
    self.assertIn('--browser=release_x64', options.passthrough_args)
    self.assertIn('-v', options.passthrough_args)
    self.assertIn('--a=b', options.passthrough_args)
    self.assertEqual(options.executable, '../../tools/perf/run_benchmark')
    self.assertEqual(options.isolated_script_test_output,
                     r'c:\a\b\c\output.json')

  def testRunPerformanceTestsTelemetryCommandGenerator_ReferenceBrowserComeLast(self):
    """This tests for crbug.com/928928."""
    options = run_performance_tests.parse_arguments([
        '../../tools/perf/run_benchmark', '--browser=release_x64',
        '--run-ref-build',
        '--test-shard-map-filename=win-10-perf_map.json',
        r'--isolated-script-test-output=c:\a\b\c\output.json',
    ])
    self.assertIn('--browser=release_x64', options.passthrough_args)
    command = run_performance_tests.TelemetryCommandGenerator(
        'fake_benchmark_name', options, is_reference=True).generate(
            'fake_output_dir')
    original_browser_arg_index = command.index('--browser=release_x64')
    reference_browser_arg_index = command.index('--browser=reference')
    self.assertTrue(reference_browser_arg_index > original_browser_arg_index)

  def testRunPerformanceTestsGtestArgsParser(self):
     options = run_performance_tests.parse_arguments([
        'media_perftests', '--non-telemetry=true', '--single-process-tests',
        '--test-launcher-retry-limit=0',
        '--isolated-script-test-filter=*::-*_unoptimized::*_unaligned::'
        '*unoptimized_aligned',
        '--gtest-benchmark-name', 'media_perftests',
        '--isolated-script-test-output=/x/y/z/output.json',
     ])
     self.assertIn('--single-process-tests', options.passthrough_args)
     self.assertIn('--test-launcher-retry-limit=0', options.passthrough_args)
     self.assertEqual(options.executable, 'media_perftests')
     self.assertEqual(options.isolated_script_test_output,
                      r'/x/y/z/output.json')
