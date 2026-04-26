package ea

import (
	"encoding/json"
	"fmt"
	"net/http"
	"time"
)

const tokenInfoURL = "https://accounts.ea.com/connect/tokeninfo"

type TokenInfo struct {
	ClientID   string `json:"client_id"`
	Scope      string `json:"scope"`
	ExpiresIn  int    `json:"expires_in"`
	PIDId      string `json:"pid_id"`
	PIDType    string `json:"pid_type"`
	UserID     string `json:"user_id"`
	PersonaID  uint64 `json:"persona_id"`
	IsUnderage bool   `json:"is_underage"`
}

// validate an access token with ea's tokeninfo endpoint
func FetchTokenInfo(accessToken string) (*TokenInfo, error) {
	client := &http.Client{Timeout: 10 * time.Second}
	resp, err := client.Get(tokenInfoURL + "?access_token=" + accessToken)
	if err != nil {
		return nil, fmt.Errorf("tokeninfo request failed: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != 200 {
		return nil, fmt.Errorf("tokeninfo returned %d", resp.StatusCode)
	}

	var info TokenInfo
	if err := json.NewDecoder(resp.Body).Decode(&info); err != nil {
		return nil, fmt.Errorf("tokeninfo decode failed: %w", err)
	}

	if info.PIDId == "" {
		return nil, fmt.Errorf("tokeninfo returned empty pid")
	}

	return &info, nil
}
