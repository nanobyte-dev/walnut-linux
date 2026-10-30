# servers/: the configuration of the machines we run; today only walnut.nanobyte.dev (its containers, nginx, the upload and the walnut-watch publisher).
`make walnut-server` puts it in place, `make walnut-server-test` tests the publisher; the site's content is in website/.
Touch it when the server side changes (docs/WALNUT-SERVER.md).
