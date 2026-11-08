import http.client, re, sys, urllib.parse, time
# Deux navigateurs sur « grym servir --sessions » (tests/sessions-concurrentes, 3.18) : chacun sa session, sa
# connexion, ses réponses ; puis les gardes de l'aiguilleur.
import os
port, jeton = open(os.path.join(sys.argv[1], 'adr.txt')).read().split()
def req(m, chemin, cookie=None, champs=None):
    c = http.client.HTTPConnection('127.0.0.1', int(port), timeout=30)
    h = {'Host': f'127.0.0.1:{port}'}
    corps = None
    if cookie: h['Cookie'] = cookie
    if champs is not None:
        corps = urllib.parse.urlencode(champs)
        h['Origin'] = f'http://127.0.0.1:{port}'
        h['Content-Type'] = 'application/x-www-form-urlencoded'
    c.request(m, chemin, body=corps, headers=h)
    r = c.getresponse()
    return r.status, dict(r.getheaders()), r.read().decode('utf-8', 'replace')
def texte(b): return re.sub(r'\s+', ' ', re.sub(r'<[^>]+>', ' ', re.sub(r'<style.*?</style>', '', b, flags=re.S))).strip()[:160]
def entrer():
    s, h, b = req('GET', '/?jeton=' + jeton)
    return h['Set-Cookie'].split(';')[0]
def q(b): return re.search(r'name="q" value="(\d+)"', b).group(1)
def envoyer(ck, champs):
    s, h, b = req('GET', '/', ck)
    d = {'q': q(b), 'action': 'envoyer'}; d.update(champs)
    s, h, b = req('POST', '/reponse', ck, d)
    if s in (302, 303): s, h, b = req('GET', '/', ck)
    return texte(b)
A = entrer(); B = entrer()
print('A crée :', envoyer(A, {'c0': 'ana', 'c1': 'secret123', 'c2': 'secret123'}))
print('B page :', texte(req('GET', '/', B)[2]))
print('B se connecte :', envoyer(B, {'c0': 'ana', 'c1': 'secret123'}))
print('A répond :', envoyer(A, {'c0': 'rouge'}))
print('B répond :', envoyer(B, {'c0': 'vert'}))
print('sans cookie :', req('GET', '/')[0], ' mauvais jeton :', req('GET', '/?jeton=00')[0])
time.sleep(3)   # la session finie sert sa page finale deux secondes encore (ATTENTE_FIN), puis s'arrête
print('A revient :', texte(req('GET', '/', A)[2]))
c = http.client.HTTPConnection('127.0.0.1', int(port), timeout=10)
c.request('GET', '/', headers={'Host': 'exemple.ch', 'Cookie': A}); print('autre hôte :', c.getresponse().status)
s, h, b = req('GET', '/', A); d = {'q': q(b), 'action': 'envoyer', 'c0': 'ana', 'c1': 'secret123'}
c = http.client.HTTPConnection('127.0.0.1', int(port), timeout=10)
c.request('POST', '/reponse', body=urllib.parse.urlencode(d), headers={'Host': f'127.0.0.1:{port}', 'Cookie': A, 'Origin': 'http://evil.ch', 'Content-Type': 'application/x-www-form-urlencoded'})
print('autre origine :', c.getresponse().status)
