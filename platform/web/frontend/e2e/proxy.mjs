import http from 'node:http'

const target = new URL(process.env.E2E_UPSTREAM ?? 'http://web:8080')

http.createServer((request, response) => {
  const upstream = http.request({
    hostname: target.hostname,
    port: target.port,
    method: request.method,
    path: request.url,
    headers: { ...request.headers, host: target.host },
  }, (incoming) => {
    response.writeHead(incoming.statusCode ?? 502, incoming.headers)
    incoming.pipe(response)
  })
  upstream.on('error', () => {
    if (!response.headersSent) response.writeHead(502)
    response.end('upstream unavailable')
  })
  request.pipe(upstream)
}).listen(8099, '127.0.0.1')
