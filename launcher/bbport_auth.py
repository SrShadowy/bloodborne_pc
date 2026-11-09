# SPDX-License-Identifier: GPL-2.0-or-later
"""A Global server's own accounts (docs/HOSTED_SERVER.md, "Global servers: accounts"): a name and a
password, sent to that server only, over HTTPS only, with its certificate checked. The password is
never written anywhere and never logged: it goes in one request's body and the launcher keeps only
the signed 30-day secret the server answers (bbport_coop.signed_in_global)."""
import hashlib
import json
import ssl
import threading
import urllib.error
import urllib.request

NAME_MIN, NAME_MAX = 3, 20
PASSWORD_MIN, PASSWORD_MAX = 10, 128
NAME_CHARACTERS = set('abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.')
MAX_WORK_BITS = 28  # more than a few seconds of this launcher's work: a server we do not want

PLAIN_HTTP_TEXT = 'A Global server must be reached over HTTPS: its address has to start with https://.'
NO_SERVER_TEXT = 'The server does not answer: try again in a minute.'
NOT_GLOBAL_TEXT = 'This server has no accounts.'
WRONG_TEXT = 'Wrong name or password.'
WRONG_CODE_TEXT = 'Wrong name or recovery code.'
WAIT_TEXT = 'Too many wrong tries: wait {} s and try again.'
BUSY_TEXT = 'The server is busy: try again in a minute.'
TAKEN_TEXT = 'That name is taken: choose another.'
SIGNUPS_TEXT = 'Too many new accounts from your address: try again in an hour.'
NAME_LENGTH_TEXT = 'A name has 3 to 20 characters.'
NAME_CHARACTERS_TEXT = 'A name has only letters, digits, _ - and . in it.'
SHORT_TEXT = 'A password has at least 10 characters.'
LONG_TEXT = 'A password has at most 128 characters.'
COMMON_TEXT = 'That password is too common: choose another.'
HAS_NAME_TEXT = 'The password must not contain your name.'
SAME_TEXT = 'The new password is the old one.'
CERTIFICATE_TEXT = 'The server\'s certificate is not valid: not sent.'
REFUSED_TEXT = 'The server refused it: try again.'

PROBLEMS = {('name', 'length'): NAME_LENGTH_TEXT, ('name', 'characters'): NAME_CHARACTERS_TEXT,
            ('password', 'short'): SHORT_TEXT, ('password', 'long'): LONG_TEXT,
            ('password', 'common'): COMMON_TEXT, ('password', 'name'): HAS_NAME_TEXT}


class AuthError(RuntimeError):
    """What stopped it, in words for the player (the English text; the window translates it). `args[1]`
    holds a number for the text when it has a {}."""

    def text(self):
        return str(self.args[0])


def https_base(url):
    """The server's address without the slash at its end; AuthError unless it is https."""
    url = (url or '').strip().rstrip('/')
    if not url.lower().startswith('https://') or len(url) <= len('https://'):
        raise AuthError(PLAIN_HTTP_TEXT)
    return url


class _NoRedirects(urllib.request.HTTPRedirectHandler):
    """A redirect could send the password on to plain http or another host: never followed."""

    def redirect_request(self, *_args, **_kwargs):
        return None


def _opener(context):
    # The system's certificates and host name checks (ssl.create_default_context): a server whose
    # certificate does not hold is never sent a password.
    context = context or ssl.create_default_context()
    return urllib.request.build_opener(urllib.request.HTTPSHandler(context=context), _NoRedirects,
                                       urllib.request.ProxyHandler({}))


def _call(url, path, body=None, context=None, opener=None):
    """(status, answer) of `path` on the server; AuthError when it does not answer."""
    base = https_base(url)
    data = None if body is None else json.dumps(body).encode('utf-8')
    request = urllib.request.Request(base + path, data=data, method='GET' if body is None else 'POST',
                                     headers={'Content-Type': 'application/json'})
    try:
        with (opener or _opener(context)).open(request, timeout=30) as answer:
            return answer.status, json.loads(answer.read() or b'{}')
    except urllib.error.HTTPError as error:
        try:
            return error.code, json.loads(error.read() or b'{}')
        except (OSError, ValueError):
            return error.code, {}
    except urllib.error.URLError as error:
        if isinstance(error.reason, ssl.SSLError):
            raise AuthError(CERTIFICATE_TEXT) from None
        raise AuthError(NO_SERVER_TEXT) from None
    except ssl.SSLError:
        raise AuthError(CERTIFICATE_TEXT) from None
    except (OSError, ValueError):
        raise AuthError(NO_SERVER_TEXT) from None


def _refusal(status, answer, wrong=WRONG_TEXT):
    """The AuthError for a refused request."""
    why = answer.get('error', '') if isinstance(answer, dict) else ''
    if status == 401:
        return AuthError(wrong)
    if status == 429 and why == 'wait':
        return AuthError(WAIT_TEXT, int(answer.get('seconds', 1)))
    if status == 429 and why == 'signups':
        return AuthError(SIGNUPS_TEXT)
    if status in (429, 503):
        return AuthError(BUSY_TEXT)
    if status == 409:
        return AuthError(TAKEN_TEXT)
    if status == 400 and (why, answer.get('problem')) in PROBLEMS:
        return AuthError(PROBLEMS[why, answer['problem']])
    if status == 404:
        return AuthError(NOT_GLOBAL_TEXT)
    return AuthError(REFUSED_TEXT)


def _account(status, answer, wrong=WRONG_TEXT):
    if status != 200 or not answer.get('id') or not answer.get('secret'):
        raise _refusal(status, answer, wrong)
    account = {'id': answer['id'], 'secret': answer['secret'], 'name': answer.get('name', '')}
    if answer.get('recovery_code'):
        account['recovery_code'] = answer['recovery_code']
    return account


# --- before anything is sent: what the server would refuse anyway -----------------------------------

def name_problem(name):
    if not NAME_MIN <= len(name) <= NAME_MAX:
        return NAME_LENGTH_TEXT
    if not set(name) <= NAME_CHARACTERS:
        return NAME_CHARACTERS_TEXT
    return None


def password_problem(password, name):
    if len(password) < PASSWORD_MIN:
        return SHORT_TEXT
    if len(password) > PASSWORD_MAX:
        return LONG_TEXT
    if len(name) >= NAME_MIN and name.lower() in password.lower():
        return HAS_NAME_TEXT
    return None


def strength(password):
    """A hint while typing: 0 (too short) to 3 (strong). Length counts most; no rules about what it is
    made of (the server refuses the common ones)."""
    if len(password) < PASSWORD_MIN:
        return 0
    kinds = sum(any(test(c) for c in password)
                for test in (str.islower, str.isupper, str.isdigit, lambda c: not c.isalnum()))
    distinct = len(set(password))
    score = 1 + (len(password) >= 14) + (len(password) >= 20 or (len(password) >= 12 and kinds >= 3))
    if distinct <= 3:
        score = 1
    return min(score, 3)


# --- the server's calls -------------------------------------------------------------------------

def config(url, context=None, opener=None):
    status, answer = _call(url, '/bb/auth/config', context=context, opener=opener)
    if status != 200 or 'password' not in answer.get('sign_in', []):
        raise AuthError(NOT_GLOBAL_TEXT)
    return answer


def solve(challenge, name, bits, stop=None):
    """The signup's work (the server's password_accounts.h WorkDone): a nonce such that SHA-256 of
    "bb signup v1|<challenge>|<name in lower case>|<nonce, 8 bytes little-endian>" starts with `bits`
    zero bits."""
    if bits > MAX_WORK_BITS:
        raise AuthError(BUSY_TEXT)
    prefix = hashlib.sha256(b'bb signup v1|' + challenge.encode('ascii') + b'|' + name.lower().encode('utf-8') + b'|')
    shift = 256 - bits
    nonce = 0
    while True:
        digest = prefix.copy()
        digest.update(nonce.to_bytes(8, 'little'))
        if int.from_bytes(digest.digest(), 'big') >> shift == 0:
            return nonce
        nonce += 1
        if stop is not None and nonce % 65536 == 0 and stop.is_set():
            raise AuthError(REFUSED_TEXT)


def sign_up(url, name, password, context=None, opener=None, stop=None):
    """A new account: {'id', 'secret', 'name', 'recovery_code'}."""
    problem = name_problem(name) or password_problem(password, name)
    if problem:
        raise AuthError(problem)
    status, work = _call(url, '/bb/auth/challenge', {}, context, opener)
    if status != 200 or not work.get('challenge'):
        raise _refusal(status, work)
    nonce = solve(work['challenge'], name, int(work.get('bits', 0)), stop)
    status, answer = _call(url, '/bb/auth/sign-up', {'name': name, 'password': password,
                                                    'challenge': work['challenge'], 'nonce': nonce},
                           context, opener)
    return _account(status, answer)


def sign_in(url, name, password, context=None, opener=None):
    """The account on this PC too: {'id', 'secret', 'name'}."""
    if not name or not password:
        raise AuthError(WRONG_TEXT)
    return _account(*_call(url, '/bb/auth/sign-in', {'name': name, 'password': password}, context, opener))


def change_password(url, name, password, new_password, context=None, opener=None):
    """A new password; every other sign-in of the account ends. {'id', 'secret', 'name'}."""
    if new_password == password:
        raise AuthError(SAME_TEXT)
    problem = password_problem(new_password, name)
    if problem:
        raise AuthError(problem)
    return _account(*_call(url, '/bb/auth/change', {'name': name, 'password': password,
                                                    'new_password': new_password}, context, opener))


def recover(url, name, code, new_password, context=None, opener=None):
    """Forgot password: the recovery code gives a new password and a new code; every other sign-in
    ends. {'id', 'secret', 'name', 'recovery_code'}."""
    problem = password_problem(new_password, name)
    if problem:
        raise AuthError(problem)
    status, answer = _call(url, '/bb/auth/recover', {'name': name, 'recovery_code': code,
                                                     'new_password': new_password}, context, opener)
    return _account(status, answer, WRONG_CODE_TEXT)


def sign_out(url, account, secret, context=None, opener=None):
    """Tells the server this account's sign-ins until now are dead. Best effort: the launcher forgets
    the secret anyway."""
    try:
        return _call(url, '/bb/auth/sign-out', {'id': account, 'secret': secret}, context, opener)[0] == 200
    except AuthError:
        return False


def run_thread(call, done, *args, **options):
    """`call` off the window's thread: done(account, None) or done(None, AuthError)."""
    def run():
        try:
            done(call(*args, **options), None)
        except AuthError as error:
            done(None, error)
    threading.Thread(target=run, daemon=True).start()
