package ticket

import (
	"crypto/hmac"
	"crypto/sha256"
	"encoding/base64"
	"encoding/json"
	"errors"
	"time"
)

// session ticket issued by master, validated offline by relay/dll
// format: base64(payload) + "." + base64(hmac-sha256(secret, payload_b64))

const DefaultTicketTTL = 30 * time.Minute

type Payload struct {
	PID         string `json:"pid"`
	PSID        uint64 `json:"psid"`
	DisplayName string `json:"dn"`
	IssuedAt    int64  `json:"iat"`
	ExpiresAt   int64  `json:"exp"`
}

func Issue(secret []byte, pid string, psid uint64, displayName string, ttl time.Duration) string {
	now := time.Now()
	p := Payload{
		PID:         pid,
		PSID:        psid,
		DisplayName: displayName,
		IssuedAt:    now.Unix(),
		ExpiresAt:   now.Add(ttl).Unix(),
	}
	payloadJSON, _ := json.Marshal(p)
	payloadB64 := base64.RawURLEncoding.EncodeToString(payloadJSON)

	mac := hmac.New(sha256.New, secret)
	mac.Write([]byte(payloadB64))
	sig := base64.RawURLEncoding.EncodeToString(mac.Sum(nil))

	return payloadB64 + "." + sig
}

func Validate(secret []byte, raw string) (*Payload, error) {
	dot := -1
	for i := len(raw) - 1; i >= 0; i-- {
		if raw[i] == '.' {
			dot = i
			break
		}
	}
	if dot < 0 {
		return nil, errors.New("invalid ticket format")
	}

	payloadB64 := raw[:dot]
	sigB64 := raw[dot+1:]

	// verify signature
	mac := hmac.New(sha256.New, secret)
	mac.Write([]byte(payloadB64))
	expectedSig := mac.Sum(nil)

	sig, err := base64.RawURLEncoding.DecodeString(sigB64)
	if err != nil {
		return nil, errors.New("invalid ticket signature encoding")
	}

	if !hmac.Equal(sig, expectedSig) {
		return nil, errors.New("invalid ticket signature")
	}

	// parse payload
	payloadJSON, err := base64.RawURLEncoding.DecodeString(payloadB64)
	if err != nil {
		return nil, errors.New("invalid ticket payload encoding")
	}

	var p Payload
	if err := json.Unmarshal(payloadJSON, &p); err != nil {
		return nil, errors.New("invalid ticket payload")
	}

	// check expiry
	if time.Now().Unix() > p.ExpiresAt {
		return nil, errors.New("ticket expired")
	}

	return &p, nil
}
