package ea

import (
	"crypto"
	"crypto/rsa"
	"crypto/sha256"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"strings"
	"time"
)

// ea jwt claims follow the nexus structure

type NexusClaims struct {
	PID         string          `json:"pid"`
	UID         string          `json:"uid"`
	PSID        uint64          `json:"psid"`
	DVID        string          `json:"dvid"`
	UserInfo    UserInfoClaims  `json:"uif"`
	PersonaInfo []PersonaClaims `json:"psif"`
}

type UserInfoClaims struct {
	Country  string `json:"cty"`
	Language string `json:"lan"`
	Status   string `json:"sta"`
}

type PersonaClaims struct {
	ID          uint64 `json:"id"`
	Namespace   string `json:"ns"`
	DisplayName string `json:"dis"`
	Nickname    string `json:"nic"`
}

type EAJWTClaims struct {
	Nexus    NexusClaims `json:"nexus"`
	Subject  string      `json:"sub"`
	Issuer   string      `json:"iss"`
	IssuedAt int64       `json:"iat"`
	Expires  int64       `json:"exp"`
}

// get the main ea persona (cem_ea_id namespace)
func (c *EAJWTClaims) Persona() *PersonaClaims {
	for i := range c.Nexus.PersonaInfo {
		if c.Nexus.PersonaInfo[i].Namespace == "cem_ea_id" {
			return &c.Nexus.PersonaInfo[i]
		}
	}
	if len(c.Nexus.PersonaInfo) > 0 {
		return &c.Nexus.PersonaInfo[0]
	}
	return nil
}

func (c *EAJWTClaims) DisplayName() string {
	if p := c.Persona(); p != nil {
		return p.DisplayName
	}
	return ""
}

// parse jwt claims without signature verification
// only use after tokeninfo has confirmed the token is valid
func ParseClaimsUnsafe(token string) (*EAJWTClaims, error) {
	parts := strings.SplitN(token, ".", 3)
	if len(parts) < 2 {
		return nil, errors.New("not a jwt")
	}

	claimsJSON, err := base64.RawURLEncoding.DecodeString(parts[1])
	if err != nil {
		return nil, fmt.Errorf("invalid jwt claims: %w", err)
	}

	var claims EAJWTClaims
	if err := json.Unmarshal(claimsJSON, &claims); err != nil {
		return nil, fmt.Errorf("invalid jwt claims json: %w", err)
	}

	return &claims, nil
}

// validate and parse an EA JWT token
func ValidateToken(cache *JWKSCache, token string) (*EAJWTClaims, error) {
	parts := strings.SplitN(token, ".", 3)
	if len(parts) != 3 {
		return nil, errors.New("invalid jwt format")
	}

	// parse header for kid
	headerJSON, err := base64.RawURLEncoding.DecodeString(parts[0])
	if err != nil {
		return nil, fmt.Errorf("invalid jwt header: %w", err)
	}

	var header struct {
		Alg string `json:"alg"`
		Kid string `json:"kid"`
	}
	if err := json.Unmarshal(headerJSON, &header); err != nil {
		return nil, fmt.Errorf("invalid jwt header json: %w", err)
	}

	if header.Alg != "RS256" {
		return nil, fmt.Errorf("unsupported alg: %s", header.Alg)
	}

	// get the signing key
	pubKey, err := cache.GetKey(header.Kid)
	if err != nil {
		return nil, fmt.Errorf("key lookup failed: %w", err)
	}

	// verify signature
	signedContent := parts[0] + "." + parts[1]
	sigBytes, err := base64.RawURLEncoding.DecodeString(parts[2])
	if err != nil {
		return nil, fmt.Errorf("invalid jwt signature encoding: %w", err)
	}

	hash := sha256.Sum256([]byte(signedContent))
	if err := rsa.VerifyPKCS1v15(pubKey, crypto.SHA256, hash[:], sigBytes); err != nil {
		return nil, errors.New("jwt signature verification failed")
	}

	// parse claims
	claimsJSON, err := base64.RawURLEncoding.DecodeString(parts[1])
	if err != nil {
		return nil, fmt.Errorf("invalid jwt claims: %w", err)
	}

	var claims EAJWTClaims
	if err := json.Unmarshal(claimsJSON, &claims); err != nil {
		return nil, fmt.Errorf("invalid jwt claims json: %w", err)
	}

	// check expiry
	now := time.Now().Unix()
	if claims.Expires > 0 && now > claims.Expires {
		return nil, errors.New("ea token expired")
	}

	// check issuer
	if claims.Issuer != "" && claims.Issuer != "accounts.ea.com" {
		return nil, fmt.Errorf("unexpected issuer: %s", claims.Issuer)
	}

	// must have a player id
	if claims.Nexus.PID == "" {
		return nil, errors.New("ea token missing player id")
	}

	return &claims, nil
}
