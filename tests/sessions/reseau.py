# L'aiguilleur sur le réseau, en TLS (tests/sessions-reseau, docs/v2.md, § 12) : un client qui vérifie le certificat par
# l'autorité du cabinet ; les noms du certificat comme seuls hôtes ; le cookie « Secure » ; un client en clair, ou sans
# l'autorité, refusé ; l'aiguilleur toujours debout ensuite.
import http.client, os, re, socket, ssl, sys
d, port = sys.argv[1], int(sys.argv[2])
jeton = re.search(r'jeton=([0-9a-f]+)', open(os.path.join(d, 'ag.txt')).read()).group(1)
ctx = ssl.create_default_context(cafile=os.path.join(d, 'cert', 'autorite.crt'))
def req(chemin, cookie=None, hote=None):
    c = http.client.HTTPSConnection('127.0.0.1', port, context=ctx, timeout=15)
    h = {'Host': hote or f'praxis.local:{port}'}
    if cookie: h['Cookie'] = cookie
    c.request('GET', chemin, headers=h); r = c.getresponse(); return r.status, dict(r.getheaders()), r.read().decode()
s, h, b = req('/?jeton=' + jeton)
cookie = h.get('Set-Cookie', '')
print('entrée :', s, 'Secure' in cookie, 'HttpOnly' in cookie)
ck = cookie.split(';')[0]
s, h, b = req('/', ck)
print('page :', s, 'type="password"' in b)
print('hôte 127.0.0.1 :', req('/', ck, f'127.0.0.1:{port}')[0])
print('hôte inconnu :', req('/', ck, f'evil.ch:{port}')[0])
s = socket.create_connection(('127.0.0.1', port)); s.settimeout(8)
s.sendall(f'GET / HTTP/1.1\r\nHost: praxis.local:{port}\r\n\r\n'.encode())
try: print('en clair :', 'réponse' if s.recv(100) else 'fermé')
except Exception: print('en clair : fermé')
try:
    http.client.HTTPSConnection('127.0.0.1', port, context=ssl.create_default_context(), timeout=5).request('GET', '/')
    print('sans autorité : accepté')
except ssl.SSLCertVerificationError: print('sans autorité : refusé')
print('toujours debout :', req('/', ck)[0])
