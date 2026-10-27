"""Unified-launch recovery fixtures; no debugger or game process."""
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

import install_upload_observer as installer
from run_launch_tests import finish_upload_install


class UploadInstallLaunchTests(unittest.TestCase):
    def session(self, valid=False):
        session = SimpleNamespace(code_valid=valid, error='initial failure')
        session.state = lambda outcome: dict(outcome=outcome, error=session.error,
                                             code_valid=session.code_valid, event_held=True, attached=True)
        return session

    def test_success_keeps_exact_install_result(self):
        result = {'outcome': 'committed_detached_accept_requested'}
        with patch.object(installer, 'install', return_value=result) as install:
            self.assertIs(finish_upload_install('backend', {'symbol': 1}, 7, Mock()), result)
            install.assert_called_once_with('backend', {'symbol': 1}, expected_nonce=7)

    def test_unsafe_recovery_does_not_escape_on_report_or_observation_failure(self):
        session = self.session()
        result = {'outcome': 'owned_game_terminated_verified'}
        with patch.object(installer, 'install', side_effect=installer.HeldRecovery(session, 'unsafe')):
            with patch.object(installer, 'recover_once', side_effect=[OSError('pending exit'), result]) as recover:
                sleep = Mock(side_effect=KeyboardInterrupt)
                observed = finish_upload_install('backend', {}, 7, Mock(side_effect=OSError('disk full')), sleep=sleep)
        self.assertIs(observed, result)
        self.assertEqual(recover.call_count, 2)
        for call in recover.call_args_list:
            self.assertEqual(call.args, (session, 'terminate'))
            self.assertEqual(call.kwargs, dict(owned_game=True, terminate_on_unrecoverable=True))
        sleep.assert_called_once_with(.25)

    def test_valid_patch_recovery_retries_detach_without_termination(self):
        session = self.session(valid=True)
        result = {'outcome': 'valid_detached_unaccepted'}
        with patch.object(installer, 'install', side_effect=installer.HeldRecovery(session, 'detach')):
            with patch.object(installer, 'recover_once', return_value=result) as recover:
                self.assertIs(finish_upload_install('backend', {}, 7, Mock()), result)
        self.assertEqual(recover.call_args.args, (session, 'retry-detach'))


if __name__ == '__main__':
    unittest.main()
