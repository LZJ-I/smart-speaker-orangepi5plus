const fs = require('fs')
const http = require('http')
const https = require('https')
const path = require('path')
const childProcess = require('child_process')
const { URL } = require('url')

const CONFIG_PATH = path.join(__dirname, '..', 'data', 'config', 'music-service.toml')

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
      'default_source = "kw"',
      'search_sources = "kw"',
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
    typeof parsed.default_source === 'string' && parsed.default_source ? parsed.default_source : 'kw'
  const sourceScriptRaw =
    typeof parsed.music_source_script === 'string' && parsed.music_source_script
      ? parsed.music_source_script
      : '../music-source/lx.js'
  return {
    host: typeof parsed.host === 'string' && parsed.host ? parsed.host : '127.0.0.1',
    port: Number.isInteger(parsed.port) && parsed.port > 0 ? parsed.port : 9300,
    provider: typeof parsed.provider === 'string' && parsed.provider ? parsed.provider : 'lx',
    defaultSource,
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
  const safePageSize = safeNumber(pageSize, 10)
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

function requestText(urlString, options = {}, redirectCount = 0) {
  const payload = JSON.stringify({
    url: urlString,
    method: options.method || 'GET',
    headers: options.headers || {},
    body: options.body || '',
    timeout_ms: options.timeout || 10000
  })
  const pythonScript = [
    'import json, sys, urllib.request',
    'req = json.loads(sys.argv[1])',
    'body = req.get("body")',
    'data = body.encode("utf-8") if body else None',
    'request = urllib.request.Request(req["url"], data=data, headers=req.get("headers") or {}, method=req.get("method") or "GET")',
    'with urllib.request.urlopen(request, timeout=max(float(req.get("timeout_ms", 10000)) / 1000.0, 1.0)) as resp:',
    '    text = resp.read().decode("utf-8", "ignore")',
    '    print(json.dumps({"statusCode": int(getattr(resp, "status", 0) or 0), "headers": dict(resp.headers), "text": text}))'
  ].join('\n')
  return new Promise((resolve, reject) => {
    try {
      const output = childProcess.execFileSync('python3', ['-c', pythonScript, payload], {
        encoding: 'utf8',
        timeout: (options.timeout || 10000) + 5000,
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

async function searchKwSongs(keyword, page, pageSize) {
  const safePage = safeNumber(page, 1)
  const safeSize = safeNumber(pageSize, 10)
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
        headers: { 'User-Agent': 'Mozilla/5.0 smart-speaker-music-service' }
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
  const safeSize = safeNumber(pageSize, 10)
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
      'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 smart-speaker-music-service'
    }
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
  const safeSize = safeNumber(pageSize, 10)
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

const songSearchBySource = {
  kw: searchKwSongs,
  wy: searchWySongs
}

async function searchSongs(keyword, page, pageSize, config) {
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
    total_pages: rows.length === 0 ? 0 : Math.ceil(rows.length / safeNumber(pageSize, 10))
  }
}

async function searchPlaylists(keyword, page, pageSize, config) {
  const srcs =
    config.playlistSearchSources && config.playlistSearchSources.length
      ? config.playlistSearchSources
      : ['kw', 'wy']
  const safePage = safeNumber(page, 1)
  const safeSize = safeNumber(pageSize, 10)
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
        const pageSize = safeNumber(body.page_size, 10)
        const result = await searchSongs(keyword, page, pageSize, config)
        logListPreview('search.song', keyword, result.result, result.items)
        return json(res, 200, result)
      }

      if (pathname === '/music/search/playlist') {
        const keyword = String(body.keyword || '').trim()
        const page = safeNumber(body.page, 1)
        const pageSize = safeNumber(body.page_size, 10)
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
        return json(res, 200, {
          result: 'empty',
          kind: 'artist',
          items: [],
          page: safeNumber(body.page, 1),
          total: 0,
          total_pages: 0
        })
      }

      if (pathname === '/music/artist/hot') {
        return json(res, 200, {
          result: 'empty',
          kind: 'song',
          items: [],
          page: 1,
          total: 0,
          total_pages: 0
        })
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
