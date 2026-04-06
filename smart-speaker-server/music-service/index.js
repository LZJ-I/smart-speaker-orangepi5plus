const fs = require('fs')
const http = require('http')
const https = require('https')
const path = require('path')
const childProcess = require('child_process')
const { URL } = require('url')

const CONFIG_PATH = path.join(__dirname, '..', 'data', 'config', 'music-service.toml')
const DEFAULT_MUSIC_PAGE_SIZE = 30

function ensureDir(dirPath) {
  fs.mkdirSync(dirPath, { recursive: true })
}

function ensureConfigFile() {
  if (fs.existsSync(CONFIG_PATH)) return
  ensureDir(path.dirname(CONFIG_PATH))
  fs.writeFileSync(
    CONFIG_PATH,
    [
      '# Node 音乐子服务监听配置',
      'host = "127.0.0.1"',
      'port = 9300',
      '',
      '# 当前优先走 LX 风格 provider',
      'provider = "lx"',
      'default_source = "all"',
      'default_leaderboard_source = "wy"',
      'default_leaderboard_id = "3778678"',
      'search_sources = "kw,wy"',
      'playlist_search_sources = "kw,wy"',
      'resolve_quality = "320k"',
      'music_source_script = "../music-source/lx.js"',
      '',
      '# 脚本不可用时可用这里兜底',
      'resolver_api_url = ""',
      'resolver_api_key = ""',
      ''
    ].join('\n'),
    'utf8'
  )
}

function parseToml(content) {
  const result = {}
  for (const rawLine of content.split(/\r?\n/)) {
    const line = rawLine.trim()
    if (!line || line.startsWith('#')) continue
    const idx = line.indexOf('=')
    if (idx === -1) continue
    const key = line.slice(0, idx).trim()
    let value = line.slice(idx + 1).trim()
    if (value.startsWith('"') && value.endsWith('"')) {
      value = value.slice(1, -1)
    } else if (/^-?\d+$/.test(value)) {
      value = Number(value)
    }
    result[key] = value
  }
  return result
}

function parseCsv(text, fallback) {
  const values = String(text || '')
    .split(',')
    .map((item) => item.trim())
    .filter(Boolean)
  return values.length > 0 ? values : fallback
}

function readConfig() {
  ensureConfigFile()
  const parsed = parseToml(fs.readFileSync(CONFIG_PATH, 'utf8'))
  const configDir = path.dirname(CONFIG_PATH)
  const defaultSource =
    typeof parsed.default_source === 'string' && parsed.default_source ? parsed.default_source : 'all'
  const sourceScriptRaw =
    typeof parsed.music_source_script === 'string' && parsed.music_source_script
      ? parsed.music_source_script
      : '../music-source/lx.js'
  return {
    host: typeof parsed.host === 'string' && parsed.host ? parsed.host : '127.0.0.1',
    port: Number.isInteger(parsed.port) && parsed.port > 0 ? parsed.port : 9300,
    provider: typeof parsed.provider === 'string' && parsed.provider ? parsed.provider : 'lx',
    defaultSource,
    defaultLeaderboardSource:
      typeof parsed.default_leaderboard_source === 'string' && parsed.default_leaderboard_source
        ? parsed.default_leaderboard_source
        : 'wy',
    defaultLeaderboardId:
      typeof parsed.default_leaderboard_id === 'string' && parsed.default_leaderboard_id
        ? parsed.default_leaderboard_id
        : '3778678',
    searchSources: parseCsv(parsed.search_sources, [defaultSource]),
    playlistSearchSources: parseCsv(parsed.playlist_search_sources, ['kw', 'wy']),
    resolveQuality:
      typeof parsed.resolve_quality === 'string' && parsed.resolve_quality ? parsed.resolve_quality : '320k',
    sourceScriptPath: path.isAbsolute(sourceScriptRaw)
      ? sourceScriptRaw
      : path.resolve(configDir, sourceScriptRaw),
    resolverApiUrl:
      typeof parsed.resolver_api_url === 'string' && parsed.resolver_api_url ? parsed.resolver_api_url : '',
    resolverApiKey:
      typeof parsed.resolver_api_key === 'string' && parsed.resolver_api_key ? parsed.resolver_api_key : ''
  }
}

function normalizeKeyword(keyword) {
  return String(keyword || '').trim().toLowerCase()
}

function safeNumber(value, fallback) {
  const number = Number(value)
  return Number.isFinite(number) && number > 0 ? number : fallback
}

function paginate(items, page, pageSize) {
  const safePage = safeNumber(page, 1)
  const safePageSize = safeNumber(pageSize, DEFAULT_MUSIC_PAGE_SIZE)
  const total = items.length
  const totalPages = total === 0 ? 0 : Math.ceil(total / safePageSize)
  const start = (safePage - 1) * safePageSize
  return {
    items: items.slice(start, start + safePageSize),
    page: safePage,
    total,
    total_pages: totalPages
  }
}

function objStrToJson(text) {
  const translationMap = {
    "{'": '{"',
    "'}\n": '"}',
    "'}": '"}',
    "':'": '":"',
    "','": '","',
    "':{'": '":{"',
    "':['": '":["',
    "'}],'": '"}],"',
    "':[{'": '":[{"',
    "'},'": '"},"',
    "'},{'": '"},{"',
    "':[],'": '":[],"',
    "':{},'": '":{},"',
    "'}]}": '"}]}'
  }
  return JSON.parse(
    text.replace(/(^{'|'}\n$|'}$|':'|','|':\[{'|'}\],'|':{'|'},'|'},{'|':\['|':\[\],'|':{},'|'}]})/g,
      (item) => translationMap[item])
  )
}

const HTTP_SEARCH_TIMEOUT_MS = 15000

function requestText(urlString, options = {}, redirectCount = 0) {
  const payload = JSON.stringify({
    url: urlString,
    method: options.method || 'GET',
    headers: options.headers || {},
    body: options.body || '',
    timeout_ms: options.timeout || HTTP_SEARCH_TIMEOUT_MS
  })
  const pythonScript = [
    'import json, sys, urllib.request',
    'req = json.loads(sys.argv[1])',
    'body = req.get("body")',
    'data = body.encode("utf-8") if body else None',
    'request = urllib.request.Request(req["url"], data=data, headers=req.get("headers") or {}, method=req.get("method") or "GET")',
    'with urllib.request.urlopen(request, timeout=max(float(req.get("timeout_ms", 15000)) / 1000.0, 1.0)) as resp:',
    '    text = resp.read().decode("utf-8", "ignore")',
    '    print(json.dumps({"statusCode": int(getattr(resp, "status", 0) or 0), "headers": dict(resp.headers), "text": text}))'
  ].join('\n')
  return new Promise((resolve, reject) => {
    try {
      const output = childProcess.execFileSync('python3', ['-c', pythonScript, payload], {
        encoding: 'utf8',
        timeout: (options.timeout || HTTP_SEARCH_TIMEOUT_MS) + 5000,
        maxBuffer: 8 * 1024 * 1024
      })
      resolve(JSON.parse(output))
    } catch (error) {
      reject(error)
    }
  })
}

function requestJson(urlString, options = {}) {
  return requestText(urlString, options).then(({ statusCode, headers, text }) => ({
    statusCode,
    headers,
    body: text ? JSON.parse(text) : {}
  }))
}

function decodeHtml(text) {
  return String(text || '')
    .replace(/&nbsp;/g, ' ')
    .replace(/&amp;/g, '&')
    .replace(/&quot;/g, '"')
    .replace(/&#39;/g, "'")
    .replace(/&lt;/g, '<')
    .replace(/&gt;/g, '>')
}

function parseLxSourceScript(scriptPath) {
  if (!scriptPath || !fs.existsSync(scriptPath)) return null
  const script = fs.readFileSync(scriptPath, 'utf8')
  const apiUrlMatch = /const\s+API_URL\s*=\s*"([^"]+)"/.exec(script)
  const apiKeyMatch = /const\s+API_KEY\s*=\s*"([^"]+)"/.exec(script)
  const qualityMatch = /const\s+MUSIC_QUALITY\s*=\s*(\{[\s\S]*?\});/.exec(script)
  const apiUrl = apiUrlMatch ? apiUrlMatch[1] : ''
  const apiKey = apiKeyMatch ? apiKeyMatch[1] : ''
  const qualityRaw = qualityMatch ? qualityMatch[1] : '{}'
  let musicQuality = {}
  try {
    musicQuality = JSON.parse(qualityRaw)
  } catch (_) {
    musicQuality = {}
  }
  return {
    apiUrl,
    apiKey,
    musicQuality
  }
}

function getResolverInfo(config) {
  const fromScript = parseLxSourceScript(config.sourceScriptPath)
  return {
    apiUrl: config.resolverApiUrl || (fromScript ? fromScript.apiUrl : '') || '',
    apiKey: config.resolverApiKey || (fromScript ? fromScript.apiKey : '') || '',
    musicQuality: (fromScript ? fromScript.musicQuality : null) || {}
  }
}

function logListPreview(label, keyword, result, items) {
  const preview = items
    .slice(0, 3)
    .map((item) => `${item.source}/${item.id}/${item.title}/${item.subtitle}`)
    .join(' | ')
  process.stdout.write(
    `[music-service] ${label} keyword=${keyword || '<empty>'} result=${result} items.size=${items.length}${preview ? ` preview=${preview}` : ''}\n`
  )
}

function logResolvePreview(source, id, result, playUrl, title, subtitle) {
  process.stdout.write(
    `[music-service] resolve source=${source || '<empty>'} id=${id || '<empty>'} result=${result} title=${title || ''} subtitle=${subtitle || ''} play_url=${playUrl || ''}\n`
  )
}

function buildSongItem(source, id, title, subtitle, cover) {
  return {
    kind: 'song',
    source: String(source || ''),
    id: String(id || ''),
    title: String(title || ''),
    subtitle: String(subtitle || ''),
    cover: String(cover || '')
  }
}

function buildPlaylistItem(source, id, title, subtitle, cover, songCount) {
  return {
    kind: 'playlist',
    source: String(source || ''),
    id: String(id || ''),
    title: String(title || ''),
    subtitle: String(subtitle || ''),
    cover: String(cover || ''),
    song_count: safeNumber(songCount, 0)
  }
}

function buildArtistItem(source, id, title, subtitle, cover) {
  return {
    kind: 'artist',
    source: String(source || ''),
    id: String(id || ''),
    title: String(title || ''),
    subtitle: String(subtitle || ''),
    cover: String(cover || '')
  }
}

function emptySongSearchPage(page, pageSize) {
  const safePage = safeNumber(page, 1)
  const safeSize = safeNumber(pageSize, DEFAULT_MUSIC_PAGE_SIZE)
  return {
    result: 'empty',
    kind: 'song',
    items: [],
    page: safePage,
    total: 0,
    total_pages: 0
  }
}

function withSearchTimeout(promise) {
  return Promise.race([
    promise,
    new Promise((_, reject) => setTimeout(() => reject(new Error('search timeout')), HTTP_SEARCH_TIMEOUT_MS))
  ])
}

function lxSimilar(a, b) {
  if (!a || !b) return 0
  let sa = String(a)
  let sb = String(b)
  if (sa.length > sb.length) {
    const t = sb
    sb = sa
    sa = t
  }
  const al = sa.length
  const bl = sb.length
  if (!bl) return 0
  const mp = new Array(bl + 1)
  for (let i = 0; i <= bl; i++) mp[i] = i
  for (let i = 1; i <= al; i++) {
    const ai = sa.charCodeAt(i - 1)
    let lt = mp[0]
    mp[0] = mp[0] + 1
    for (let j = 1; j <= bl; j++) {
      const tmp = Math.min(mp[j] + 1, mp[j - 1] + 1, lt + (ai === sb.charCodeAt(j - 1) ? 0 : 1))
      lt = mp[j]
      mp[j] = tmp
    }
  }
  return 1 - mp[bl] / bl
}

function lxSongIdKey(item) {
  return `${item.source}_${item.id}`
}

function normalizeForMatch(text) {
  return String(text || '')
    .toLowerCase()
    .replace(/[^\p{L}\p{N}]+/gu, '')
}

function splitKeywordHints(keyword) {
  const raw = String(keyword || '').trim()
  const compact = normalizeForMatch(raw)
  const compactNoDe = compact.replace(/的+/g, '')
  const hint = {
    raw,
    compact,
    compactNoDe,
    title: '',
    singer: ''
  }
  if (!compact) return hint

  const rawNoSpace = raw.replace(/\s+/g, '')
  const splitBy = (token) => {
    const idx = rawNoSpace.lastIndexOf(token)
    if (idx <= 0 || idx >= rawNoSpace.length - token.length) return null
    const left = rawNoSpace.slice(0, idx)
    const right = rawNoSpace.slice(idx + token.length)
    const singer = normalizeForMatch(left)
    const title = normalizeForMatch(right)
    if (!singer || !title) return null
    return { singer, title }
  }
  const patterns = ['唱的', '的', '-', '－', '—']
  for (const p of patterns) {
    const r = splitBy(p)
    if (r) {
      hint.singer = r.singer
      hint.title = r.title
      break
    }
  }
  return hint
}

function songDedupKey(item) {
  const title = normalizeForMatch(item && item.title)
  const subtitle = normalizeForMatch(item && item.subtitle)
  if (title || subtitle) {
    return `ts:${title}__${subtitle}`
  }
  return `id:${lxSongIdKey(item)}`
}

function songMatchScore(keywordHint, item) {
  if (!keywordHint || !keywordHint.compact) return 0
  const keywordNorm = keywordHint.compact
  const title = normalizeForMatch(item && item.title)
  const subtitle = normalizeForMatch(item && item.subtitle)
  const full = `${title}${subtitle}`
  const fullSpaced = `${title} ${subtitle}`.trim()
  let score = 0
  if (title === keywordNorm) score += 12000
  else if (title.startsWith(keywordNorm)) score += 5200
  else if (title.includes(keywordNorm)) score += 1200
  if (subtitle === keywordNorm) score += 2500
  else if (subtitle.startsWith(keywordNorm)) score += 1100
  else if (subtitle.includes(keywordNorm)) score += 500
  if (full === keywordNorm) score += 1200
  else if (full.startsWith(keywordNorm)) score += 480
  else if (full.includes(keywordNorm)) score += 160
  if (keywordHint.compactNoDe) {
    if (title === keywordHint.compactNoDe) score += 1800
    else if (full.includes(keywordHint.compactNoDe)) score += 220
  }
  if (keywordHint.title && keywordHint.singer) {
    const titleHit = title === keywordHint.title
      ? 1
      : title.startsWith(keywordHint.title)
        ? 0.7
        : title.includes(keywordHint.title)
          ? 0.45
          : 0
    const singerHit = subtitle === keywordHint.singer
      ? 1
      : subtitle.startsWith(keywordHint.singer)
        ? 0.7
        : subtitle.includes(keywordHint.singer)
          ? 0.45
          : 0
    if (titleHit > 0) score += Math.floor(5600 * titleHit)
    if (singerHit > 0) score += Math.floor(3800 * singerHit)
    if (titleHit > 0 && singerHit > 0) score += 4200
  }
  score += Math.floor(lxSimilar(keywordNorm, full) * 100)
  score += Math.floor(lxSimilar(keywordHint.raw, fullSpaced) * 100)
  return score
}

function allPageNum(total, limit) {
  const t = Number(total) || 0
  const l = safeNumber(limit, 1)
  if (!t || !l) return 0
  return Math.ceil(t / l)
}

async function searchKwSongs(keyword, page, pageSize) {
  const safePage = safeNumber(page, 1)
  const safeSize = safeNumber(pageSize, DEFAULT_MUSIC_PAGE_SIZE)
  let lastError = null
  for (let attempt = 0; attempt < 3; attempt++) {
    const searchUrl =
      'http://search.kuwo.cn/r.s?client=kt' +
      `&all=${encodeURIComponent(keyword)}` +
      `&pn=${safePage - 1}` +
      `&rn=${safeSize}` +
      '&uid=794762570&ver=kwplayer_ar_9.2.2.1&vipver=1&show_copyright_off=1&newver=1' +
      '&ft=music&cluster=0&strategy=2012&encoding=utf8&rformat=json&vermerge=1&mobi=1&issubtitle=1'
    try {
      const { body } = await requestJson(searchUrl, {
        headers: {
          'User-Agent':
            'Mozilla/5.0 (Windows NT 10.0; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/69.0.3497.100 Safari/537.36'
        },
        timeout: HTTP_SEARCH_TIMEOUT_MS
      })
      const total = Number(body.TOTAL || 0)
      const show = body.SHOW != null ? String(body.SHOW) : '1'
      if (total !== 0 && show === '0') {
        continue
      }
      const abslist = Array.isArray(body.abslist) ? body.abslist : []
      const list = abslist
        .filter((item) => item != null && item.N_MINFO != null)
        .map((item) =>
          buildSongItem(
            'kw',
            item.MUSICRID ? String(item.MUSICRID).replace(/^MUSIC_/, '') : '',
            item.SONGNAME || item.NAME || '',
            item.ARTIST || '',
            item.web_albumpic_short || ''
          )
        )
        .filter((item) => item.id)
      if (list.length === 0 && total !== 0) {
        continue
      }
      return {
        result: list.length > 0 ? 'ok' : 'empty',
        kind: 'song',
        items: list,
        page: safePage,
        total,
        total_pages: total > 0 ? Math.ceil(total / safeSize) : 0
      }
    } catch (error) {
      lastError = error
    }
  }
  if (lastError) {
    throw lastError
  }
  return {
    result: 'empty',
    kind: 'song',
    items: [],
    page: safePage,
    total: 0,
    total_pages: 0
  }
}

async function searchWySongs(keyword, page, pageSize) {
  const safePage = safeNumber(page, 1)
  const safeSize = safeNumber(pageSize, DEFAULT_MUSIC_PAGE_SIZE)
  const offset = (safePage - 1) * safeSize
  const url =
    'https://music.163.com/api/search/get?s=' +
    encodeURIComponent(keyword) +
    '&type=1&limit=' +
    safeSize +
    '&offset=' +
    offset
  const { body } = await requestJson(url, {
    headers: {
      Referer: 'https://music.163.com/',
      'User-Agent':
        'Mozilla/5.0 (Windows NT 10.0; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/69.0.3497.100 Safari/537.36'
    },
    timeout: HTTP_SEARCH_TIMEOUT_MS
  })
  const songs = body.result && Array.isArray(body.result.songs) ? body.result.songs : []
  const list = songs
    .map((t) => {
      const artist =
        Array.isArray(t.artists) && t.artists[0] && t.artists[0].name ? String(t.artists[0].name) : ''
      const cover =
        t.album && t.album.picUrl
          ? String(t.album.picUrl)
          : t.al && t.al.picUrl
            ? String(t.al.picUrl)
            : ''
      return buildSongItem('wy', String(t.id), String(t.name || ''), artist, cover)
    })
    .filter((item) => item.id)
  const total = Number((body.result && body.result.songCount) || list.length)
  return {
    result: list.length > 0 ? 'ok' : 'empty',
    kind: 'song',
    items: list,
    page: safePage,
    total,
    total_pages: total > 0 ? Math.ceil(total / safeSize) : 0
  }
}

async function searchWyPlaylists(keyword, page, pageSize) {
  const safePage = safeNumber(page, 1)
  const safeSize = safeNumber(pageSize, DEFAULT_MUSIC_PAGE_SIZE)
  const offset = (safePage - 1) * safeSize
  const url =
    'https://music.163.com/api/search/get?s=' +
    encodeURIComponent(keyword) +
    '&type=1000&limit=' +
    safeSize +
    '&offset=' +
    offset
  const { body } = await requestJson(url, {
    headers: {
      Referer: 'https://music.163.com/',
      'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 smart-speaker-music-service'
    }
  })
  const playlists =
    body.result && Array.isArray(body.result.playlists) ? body.result.playlists : []
  const list = playlists.map((p) =>
    buildPlaylistItem(
      'wy',
      String(p.id),
      String(p.name || ''),
      p.creator && p.creator.nickname ? String(p.creator.nickname) : '',
      p.coverImgUrl ? String(p.coverImgUrl) : '',
      p.trackCount
    )
  )
  const total = Number((body.result && body.result.playlistCount) || list.length)
  return {
    result: 'ok',
    kind: 'playlist',
    items: list,
    page: safePage,
    total,
    total_pages: total === 0 ? 0 : Math.ceil(total / safeSize)
  }
}

async function searchKwPlaylists(keyword, page, pageSize) {
  const searchUrl =
    'http://search.kuwo.cn/r.s' +
    `?all=${encodeURIComponent(keyword)}` +
    `&pn=${page - 1}` +
    `&rn=${pageSize}` +
    '&rformat=json&encoding=utf8&ver=mbox&vipver=MUSIC_8.7.7.0_BCS37&plat=pc&devid=28156413&ft=playlist&pay=0&needliveshow=0'
  const response = await requestText(searchUrl, {
    headers: { 'User-Agent': 'Mozilla/5.0 smart-speaker-music-service' }
  })
  const body = objStrToJson(response.text)
  const list = Array.isArray(body.abslist)
    ? body.abslist.map((item) =>
        buildPlaylistItem('kw', item.playlistid, decodeHtml(item.name), decodeHtml(item.nickname), item.pic || item.hts_pic || '', item.songnum)
      )
    : []
  return {
    result: 'ok',
    kind: 'playlist',
    items: list,
    page,
    total: Number(body.TOTAL || 0),
    total_pages: Math.ceil(Number(body.TOTAL || 0) / pageSize) || 0
  }
}

async function loadWyPlaylistDetail(id) {
  const detailUrl =
    'https://music.163.com/api/playlist/detail?id=' + encodeURIComponent(String(id)) + '&n=100'
  const { body } = await requestJson(detailUrl, {
    headers: {
      Referer: 'https://music.163.com/',
      'User-Agent': 'Mozilla/5.0 smart-speaker-music-service'
    }
  })
  const tracks = body.result && Array.isArray(body.result.tracks) ? body.result.tracks : []
  const list = tracks.map((t) => {
    const artist =
      Array.isArray(t.artists) && t.artists[0] && t.artists[0].name ? String(t.artists[0].name) : ''
    return buildSongItem('wy', String(t.id), String(t.name || ''), artist, '')
  })
  return {
    result: list.length > 0 ? 'ok' : 'empty',
    kind: 'song',
    items: list,
    page: 1,
    total: Number((body.result && body.result.trackCount) || list.length),
    total_pages: 1
  }
}

async function loadKwPlaylistDetail(id) {
  const detailUrl =
    'http://nplserver.kuwo.cn/pl.svc' +
    `?op=getlistinfo&pid=${encodeURIComponent(id)}` +
    '&pn=0&rn=100&encode=utf8&keyset=pl2012&identity=kuwo&pcmp4=1&vipver=MUSIC_9.0.5.0_W1&newver=1'
  const { body } = await requestJson(detailUrl, {
    headers: { 'User-Agent': 'Mozilla/5.0 smart-speaker-music-service' }
  })
  const list = Array.isArray(body.musiclist)
    ? body.musiclist.map((item) =>
        buildSongItem('kw', item.id, decodeHtml(item.name), decodeHtml(item.artist), '')
      )
    : []
  return {
    result: list.length > 0 ? 'ok' : 'empty',
    kind: 'song',
    items: list,
    page: 1,
    total: Number(body.total || list.length),
    total_pages: Number(body.total || 0) > 0 ? 1 : list.length > 0 ? 1 : 0
  }
}

function paginateDetailItems(result, page, pageSize) {
  const sliced = paginate(Array.isArray(result.items) ? result.items : [], page, pageSize)
  return {
    result: sliced.items.length > 0 ? 'ok' : 'empty',
    kind: result.kind || 'song',
    items: sliced.items,
    page: sliced.page,
    total: sliced.total,
    total_pages: sliced.total_pages
  }
}

async function searchWyArtists(keyword, page, pageSize) {
  const safePage = safeNumber(page, 1)
  const safeSize = safeNumber(pageSize, DEFAULT_MUSIC_PAGE_SIZE)
  const offset = (safePage - 1) * safeSize
  const url =
    'https://music.163.com/api/search/get?s=' +
    encodeURIComponent(keyword) +
    '&type=100&limit=' +
    safeSize +
    '&offset=' +
    offset
  const { body } = await requestJson(url, {
    headers: {
      Referer: 'https://music.163.com/',
      'User-Agent': 'Mozilla/5.0 smart-speaker-music-service'
    },
    timeout: HTTP_SEARCH_TIMEOUT_MS
  })
  const artists = body.result && Array.isArray(body.result.artists) ? body.result.artists : []
  const list = artists.map((item) =>
    buildArtistItem(
      'wy',
      String(item.id || ''),
      String(item.name || ''),
      String(item.alias && item.alias[0] ? item.alias[0] : ''),
      String(item.picUrl || '')
    )
  )
  return {
    result: list.length > 0 ? 'ok' : 'empty',
    kind: 'artist',
    items: list,
    page: safePage,
    total: Number((body.result && body.result.artistCount) || list.length),
    total_pages: Number((body.result && body.result.artistCount) || 0) > 0 ? Math.ceil(Number(body.result.artistCount) / safeSize) : 0
  }
}

async function loadWyArtistHot(id) {
  const detailUrl = 'https://music.163.com/api/artist/' + encodeURIComponent(String(id))
  const { body } = await requestJson(detailUrl, {
    headers: {
      Referer: 'https://music.163.com/',
      'User-Agent': 'Mozilla/5.0 smart-speaker-music-service'
    }
  })
  const tracks = Array.isArray(body.hotSongs) ? body.hotSongs : []
  const list = tracks.map((item) => {
    const artist =
      Array.isArray(item.artists) && item.artists[0] && item.artists[0].name ? String(item.artists[0].name) : ''
    return buildSongItem('wy', String(item.id || ''), String(item.name || ''), artist, '')
  })
  return {
    result: list.length > 0 ? 'ok' : 'empty',
    kind: 'song',
    items: list,
    page: 1,
    total: list.length,
    total_pages: list.length > 0 ? 1 : 0
  }
}

async function loadWyLeaderboards() {
  const url = 'https://music.163.com/api/toplist'
  const { body } = await requestJson(url, {
    headers: {
      Referer: 'https://music.163.com/',
      'User-Agent': 'Mozilla/5.0 smart-speaker-music-service'
    }
  })
  const boards = Array.isArray(body.list) ? body.list : []
  const items = boards.map((item) =>
    buildPlaylistItem(
      'wy',
      String(item.id || ''),
      String(item.name || ''),
      String(item.updateFrequency || ''),
      String(item.coverImgUrl || ''),
      Number(item.trackCount || 0)
    )
  )
  return {
    result: items.length > 0 ? 'ok' : 'empty',
    kind: 'playlist',
    items,
    page: 1,
    total: items.length,
    total_pages: items.length > 0 ? 1 : 0
  }
}

async function loadDefaultLeaderboardDetail(config, sourceHint) {
  const raw = String(sourceHint || '').trim().toLowerCase()
  const ambiguous = !raw || raw === 'all' || raw === 'auto'
  const source = ambiguous
    ? String(config.defaultLeaderboardSource || config.defaultSource || 'wy').trim().toLowerCase()
    : raw
  const boardId = String(config.defaultLeaderboardId || '').trim()
  if (!boardId) {
    return { result: 'empty', kind: 'song', items: [], page: 1, total: 0, total_pages: 0 }
  }
  if (source === 'wy') {
    return loadWyPlaylistDetail(boardId)
  }
  if (source === 'kw') {
    return loadKwPlaylistDetail(boardId)
  }
  return { result: 'empty', kind: 'song', items: [], page: 1, total: 0, total_pages: 0 }
}

const songSearchBySource = {
  kw: searchKwSongs,
  wy: searchWySongs
}

async function searchSongsAggregated(keyword, page, pageSize) {
  const safePage = safeNumber(page, 1)
  const safeSize = safeNumber(pageSize, DEFAULT_MUSIC_PAGE_SIZE)
  const LX_LIST_LIMIT = 30
  const keywordHint = splitKeywordHints(keyword)
  const settled = await Promise.allSettled([
    withSearchTimeout(searchKwSongs(keyword, safePage, LX_LIST_LIMIT)),
    withSearchTimeout(searchWySongs(keyword, safePage, LX_LIST_LIMIT))
  ])
  let combined = []
  let maxTotal = 0
  let maxAllPage = 0
  const addSource = (idx) => {
    const s = settled[idx]
    if (s.status !== 'fulfilled') return
    const p = s.value
    const t = Number(p.total) || 0
    maxTotal = Math.max(maxTotal, t)
    const ap = allPageNum(t, LX_LIST_LIMIT)
    maxAllPage = Math.max(maxAllPage, ap)
    if (ap < safePage) return
    combined.push(...(p.items || []))
  }
  addSource(0)
  addSource(1)
  const seen = new Set()
  combined = combined.filter((item) => {
    const k = songDedupKey(item)
    if (seen.has(k)) return false
    seen.add(k)
    return true
  })
  const scored = combined.map((item, i) => ({
    i,
    item,
    score: songMatchScore(keywordHint, item)
  }))
  scored.sort((a, b) => (b.score !== a.score ? b.score - a.score : a.i - b.i))
  const merged = scored.map((x) => x.item)
  if (merged.length === 0) {
    return emptySongSearchPage(safePage, safeSize)
  }
  const itemsOut = merged.slice(0, safeSize)
  const totalPages = maxTotal > 0 ? Math.ceil(maxTotal / safeSize) : 0
  return {
    result: 'ok',
    kind: 'song',
    items: itemsOut,
    page: safePage,
    total: maxTotal,
    total_pages: totalPages > 0 ? totalPages : maxAllPage
  }
}

async function searchSongs(keyword, page, pageSize, config, sourceHint) {
  const hint = String(sourceHint || '').trim().toLowerCase()
  if (!String(keyword || '').trim()) {
    return paginateDetailItems(await loadDefaultLeaderboardDetail(config, hint), page, pageSize)
  }
  if (!hint || hint === 'all' || hint === 'auto' || hint === 'default') {
    return searchSongsAggregated(keyword, page, pageSize)
  }
  const direct = songSearchBySource[hint]
  if (direct) {
    return direct(keyword, page, pageSize)
  }
  const sources =
    config.searchSources && config.searchSources.length > 0 ? config.searchSources : [config.defaultSource]
  let lastError = null
  for (const name of sources) {
    const fn = songSearchBySource[String(name || '').trim().toLowerCase()]
    if (!fn) continue
    try {
      return await fn(keyword, page, pageSize)
    } catch (error) {
      lastError = error
    }
  }
  if (lastError) throw lastError
  return { result: 'empty', kind: 'song', items: [], page, total: 0, total_pages: 0 }
}

function mergeRankPaginatePlaylists(keyword, sourcePages, page, pageSize) {
  const rows = []
  const maxLen = Math.max(0, ...sourcePages.map((p) => (p.items && p.items.length) || 0))
  for (let i = 0; i < maxLen; i++) {
    for (const p of sourcePages) {
      if (p.items && p.items[i]) rows.push(p.items[i])
    }
  }
  const k = normalizeKeyword(keyword)
  rows.sort((a, b) => {
    const ta = String(a.title || '').toLowerCase()
    const tb = String(b.title || '').toLowerCase()
    const sa = k && ta.includes(k) ? 1 : 0
    const sb = k && tb.includes(k) ? 1 : 0
    return sb - sa
  })
  const sliced = paginate(rows, page, pageSize)
  return {
    result: sliced.items.length ? 'ok' : 'empty',
    kind: 'playlist',
    items: sliced.items,
    page: sliced.page,
    total: rows.length,
    total_pages: rows.length === 0 ? 0 : Math.ceil(rows.length / safeNumber(pageSize, DEFAULT_MUSIC_PAGE_SIZE))
  }
}

async function searchPlaylists(keyword, page, pageSize, config) {
  const srcs =
    config.playlistSearchSources && config.playlistSearchSources.length
      ? config.playlistSearchSources
      : ['kw', 'wy']
  const safePage = safeNumber(page, 1)
  const safeSize = safeNumber(pageSize, DEFAULT_MUSIC_PAGE_SIZE)
  const fetchN = Math.min(80, Math.max(safePage * safeSize * 2, 24))
  const tasks = []
  if (srcs.includes('kw')) {
    tasks.push(
      searchKwPlaylists(keyword, 1, fetchN).catch(() => ({
        result: 'empty',
        kind: 'playlist',
        items: [],
        page: 1,
        total: 0,
        total_pages: 0
      }))
    )
  }
  if (srcs.includes('wy')) {
    tasks.push(
      searchWyPlaylists(keyword, 1, fetchN).catch(() => ({
        result: 'empty',
        kind: 'playlist',
        items: [],
        page: 1,
        total: 0,
        total_pages: 0
      }))
    )
  }
  if (!tasks.length) {
    return { result: 'empty', kind: 'playlist', items: [], page: safePage, total: 0, total_pages: 0 }
  }
  const settled = await Promise.all(tasks)
  return mergeRankPaginatePlaylists(keyword, settled, safePage, safeSize)
}

function pickResolveQuality(source, config, resolverInfo) {
  const support = resolverInfo.musicQuality[source]
  if (Array.isArray(support) && support.includes(config.resolveQuality)) {
    return config.resolveQuality
  }
  if (Array.isArray(support) && support.length > 0) {
    return support[0]
  }
  return config.resolveQuality
}

async function resolvePlayUrl(source, id, config) {
  const resolverInfo = getResolverInfo(config)
  if (!resolverInfo.apiUrl || !resolverInfo.apiKey) {
    return { result: 'fail', message: 'resolver api not configured' }
  }
  const quality = pickResolveQuality(source || config.defaultSource, config, resolverInfo)
  const requestUrl =
    `${resolverInfo.apiUrl.replace(/\/$/, '')}/url?source=${encodeURIComponent(source || config.defaultSource)}` +
    `&songId=${encodeURIComponent(id)}` +
    `&quality=${encodeURIComponent(quality)}`
  const { body } = await requestJson(requestUrl, {
    headers: {
      'Content-Type': 'application/json',
      'User-Agent': 'smart-speaker-music-service/0.1',
      'X-API-Key': resolverInfo.apiKey
    }
  })
  if (Number(body.code) !== 200 || !body.url) {
    return {
      result: 'fail',
      message: body.message || 'resolve failed'
    }
  }
  return {
    result: 'ok',
    kind: 'song',
    source: String(source || config.defaultSource || ''),
    id: String(id || ''),
    title: '',
    subtitle: '',
    cover: '',
    play_url: String(body.url || '')
  }
}

function json(res, statusCode, body) {
  const payload = JSON.stringify(body)
  res.writeHead(statusCode, {
    'Content-Type': 'application/json; charset=utf-8',
    'Content-Length': Buffer.byteLength(payload)
  })
  res.end(payload)
}

function readBody(req) {
  return new Promise((resolve, reject) => {
    let data = ''
    req.on('data', (chunk) => {
      data += chunk
      if (data.length > 1024 * 1024) {
        reject(new Error('body too large'))
        req.destroy()
      }
    })
    req.on('end', () => {
      if (!data) return resolve({})
      try {
        resolve(JSON.parse(data))
      } catch (error) {
        reject(error)
      }
    })
    req.on('error', reject)
  })
}

function createServer() {
  return http.createServer(async (req, res) => {
    const config = readConfig()
    const pathname = new URL(req.url, `http://${req.headers.host || '127.0.0.1'}`).pathname

    if (req.method === 'GET' && pathname === '/health') {
      const resolverInfo = getResolverInfo(config)
      return json(res, 200, {
        result: 'ok',
        service: 'music-service',
        provider: config.provider,
        search_sources: config.searchSources,
        default_source: config.defaultSource,
        resolver_api_url: resolverInfo.apiUrl,
        source_script_path: config.sourceScriptPath
      })
    }

    if (req.method !== 'POST') {
      return json(res, 404, { result: 'fail', message: 'not found' })
    }

    let body = {}
    try {
      body = await readBody(req)
    } catch (error) {
      return json(res, 400, { result: 'fail', message: error.message })
    }

    try {
      if (pathname === '/music/search/song') {
        const keyword = String(body.keyword || '').trim()
        const page = safeNumber(body.page, 1)
        const pageSize = safeNumber(body.page_size, DEFAULT_MUSIC_PAGE_SIZE)
        const sourceHint = String(body.source || '').trim()
        const result = await searchSongs(keyword, page, pageSize, config, sourceHint)
        logListPreview('search.song', keyword, result.result, result.items)
        return json(res, 200, result)
      }

      if (pathname === '/music/search/playlist') {
        const keyword = String(body.keyword || '').trim()
        const page = safeNumber(body.page, 1)
        const pageSize = safeNumber(body.page_size, DEFAULT_MUSIC_PAGE_SIZE)
        const result = await searchPlaylists(keyword, page, pageSize, config)
        logListPreview('search.playlist', keyword, result.result, result.items)
        return json(res, 200, result)
      }

      if (pathname === '/music/playlist/detail') {
        const playlistId = String(body.id || '').trim()
        const source = String(body.source || 'kw').trim().toLowerCase()
        const loadPlaylistDetail = {
          wy: loadWyPlaylistDetail,
          kw: loadKwPlaylistDetail
        }
        const loader = loadPlaylistDetail[source] || loadKwPlaylistDetail
        const result = await loader(playlistId)
        logListPreview('playlist.detail', playlistId, result.result, result.items)
        return json(res, 200, result)
      }

      if (pathname === '/music/search/artist') {
        const keyword = String(body.keyword || '').trim()
        const page = safeNumber(body.page, 1)
        const pageSize = safeNumber(body.page_size, DEFAULT_MUSIC_PAGE_SIZE)
        const source = String(body.source || config.defaultSource || 'wy').trim().toLowerCase()
        if (!keyword) {
          return json(res, 200, { result: 'empty', kind: 'artist', items: [], page, total: 0, total_pages: 0 })
        }
        if (source !== 'wy') {
          return json(res, 200, { result: 'empty', kind: 'artist', items: [], page, total: 0, total_pages: 0 })
        }
        const result = await searchWyArtists(keyword, page, pageSize)
        logListPreview('search.artist', keyword, result.result, result.items)
        return json(res, 200, result)
      }

      if (pathname === '/music/artist/hot') {
        const artistId = String(body.id || '').trim()
        const source = String(body.source || config.defaultSource || 'wy').trim().toLowerCase()
        const page = safeNumber(body.page, 1)
        const pageSize = safeNumber(body.page_size, DEFAULT_MUSIC_PAGE_SIZE)
        if (!artistId || source !== 'wy') {
          return json(res, 200, { result: 'empty', kind: 'song', items: [], page, total: 0, total_pages: 0 })
        }
        const result = paginateDetailItems(await loadWyArtistHot(artistId), page, pageSize)
        logListPreview('artist.hot', artistId, result.result, result.items)
        return json(res, 200, result)
      }

      if (pathname === '/music/leaderboard/list') {
        const source = String(body.source || config.defaultLeaderboardSource || 'wy').trim().toLowerCase()
        if (source !== 'wy') {
          return json(res, 200, { result: 'empty', kind: 'playlist', items: [], page: 1, total: 0, total_pages: 0 })
        }
        const result = await loadWyLeaderboards()
        logListPreview('leaderboard.list', source, result.result, result.items)
        return json(res, 200, result)
      }

      if (pathname === '/music/leaderboard/detail') {
        const boardId = String(body.id || config.defaultLeaderboardId || '').trim()
        const source = String(body.source || config.defaultLeaderboardSource || 'wy').trim().toLowerCase()
        const page = safeNumber(body.page, 1)
        const pageSize = safeNumber(body.page_size, DEFAULT_MUSIC_PAGE_SIZE)
        let result
        if (!boardId) {
          result = { result: 'empty', kind: 'song', items: [], page, total: 0, total_pages: 0 }
        } else if (source === 'wy') {
          result = paginateDetailItems(await loadWyPlaylistDetail(boardId), page, pageSize)
        } else if (source === 'kw') {
          result = paginateDetailItems(await loadKwPlaylistDetail(boardId), page, pageSize)
        } else {
          result = { result: 'empty', kind: 'song', items: [], page, total: 0, total_pages: 0 }
        }
        logListPreview('leaderboard.detail', boardId, result.result, result.items)
        return json(res, 200, result)
      }

      if (pathname === '/music/url/resolve') {
        const source = String(body.source || config.defaultSource || '').trim()
        const songId = String(body.id || body.song_id || '').trim()
        const title = String(body.title || '').trim()
        const subtitle = String(body.subtitle || '').trim()
        const result = await resolvePlayUrl(source, songId, config)
        result.title = title
        result.subtitle = subtitle
        logResolvePreview(source, songId, result.result, result.play_url, result.title, result.subtitle)
        return json(res, 200, result)
      }
    } catch (error) {
      return json(res, 200, {
        result: 'fail',
        message: error.message || String(error)
      })
    }

    return json(res, 404, { result: 'fail', message: 'unknown route' })
  })
}

function start() {
  const config = readConfig()
  const server = createServer()
  server.listen(config.port, config.host, () => {
    process.stdout.write(`[music-service] listening on ${config.host}:${config.port}\n`)
  })
}

if (require.main === module) {
  start()
}

module.exports = {
  createServer,
  readConfig,
  parseLxSourceScript
}

