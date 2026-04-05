const http = require('http')
const { spawn } = require('child_process')
const { readConfig } = require('./index')

function post(path, payload) {
  const config = readConfig()
  const body = JSON.stringify(payload)
  return new Promise((resolve, reject) => {
    const req = http.request(
      {
        host: config.host,
        port: config.port,
        path,
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          'Content-Length': Buffer.byteLength(body)
        }
      },
      (res) => {
        let data = ''
        res.on('data', (chunk) => {
          data += chunk
        })
        res.on('end', () => {
          try {
            resolve(JSON.parse(data))
          } catch (error) {
            reject(error)
          }
        })
      }
    )
    req.on('error', reject)
    req.write(body)
    req.end()
  })
}

async function waitUntilReady(retry = 20) {
  const config = readConfig()
  for (let i = 0; i < retry; i += 1) {
    try {
      await new Promise((resolve, reject) => {
        http
          .get(`http://${config.host}:${config.port}/health`, (res) => {
            let data = ''
            res.on('data', (chunk) => {
              data += chunk
            })
            res.on('end', () => resolve(JSON.parse(data)))
          })
          .on('error', reject)
      })
      return
    } catch (error) {
      await new Promise((resolve) => setTimeout(resolve, 150))
    }
  }
  throw new Error('music-service 未启动')
}

async function main() {
  const child = spawn(process.execPath, ['index.js'], {
    cwd: __dirname,
    stdio: 'inherit'
  })

  try {
    await waitUntilReady()
    const search = await post('/music/search/song', { keyword: '调试', page: 1, page_size: 2 })
    if (search.result !== 'ok' || !Array.isArray(search.items) || search.items.length === 0) {
      throw new Error('search song 返回异常')
    }

    const resolve = await post('/music/url/resolve', { id: search.items[0].id })
    if (resolve.result !== 'ok' || !resolve.play_url) {
      throw new Error('resolve 返回异常')
    }

    process.stdout.write('[smoke] music-service ok\n')
  } finally {
    child.kill('SIGTERM')
  }
}

main().catch((error) => {
  process.stderr.write(`${error.message}\n`)
  process.exit(1)
})
