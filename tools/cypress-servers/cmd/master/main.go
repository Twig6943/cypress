package main

import (
	"flag"
	"fmt"
	"os"

	"cypress-servers/internal/master"
)

func main() {
	cfg := master.Config{}
	flag.StringVar(&cfg.Bind, "bind", "0.0.0.0", "Bind address")
	flag.IntVar(&cfg.Port, "port", 27900, "HTTP port")
	flag.BoolVar(&cfg.BehindProxy, "behind-proxy", false, "Trust X-Forwarded-For header")
	flag.StringVar(&cfg.DBFile, "db", "cypress_master.db", "SQLite database file")
	flag.StringVar(&cfg.SecretFile, "secret-file", "moderator_secret.txt", "Moderator secret file")
	flag.Parse()

	if err := master.Run(cfg); err != nil {
		fmt.Fprintf(os.Stderr, "fatal: %v\n", err)
		os.Exit(1)
	}
}
