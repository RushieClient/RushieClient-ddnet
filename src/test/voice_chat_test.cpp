#include "test.h"

#include <game/client/components/rclient/voice/voice_chat_logic.h>

#include <gtest/gtest.h>

#include <cmath>
#include <string>

TEST(VoiceChat, ParseClientIdAcceptsValidIds)
{
	EXPECT_EQ(VoiceLogic::ParseClientId("0"), 0);
	EXPECT_EQ(VoiceLogic::ParseClientId("7"), 7);
	EXPECT_EQ(VoiceLogic::ParseClientId(std::to_string(MAX_CLIENTS - 1)), MAX_CLIENTS - 1);
}

TEST(VoiceChat, ParseClientIdRejectsInvalidIds)
{
	EXPECT_EQ(VoiceLogic::ParseClientId(""), -1);
	EXPECT_EQ(VoiceLogic::ParseClientId("-1"), -1);
	EXPECT_EQ(VoiceLogic::ParseClientId("+3"), -1);
	EXPECT_EQ(VoiceLogic::ParseClientId(" 3"), -1);
	EXPECT_EQ(VoiceLogic::ParseClientId("3 "), -1);
	EXPECT_EQ(VoiceLogic::ParseClientId("3x"), -1);
	EXPECT_EQ(VoiceLogic::ParseClientId("0x10"), -1);
	EXPECT_EQ(VoiceLogic::ParseClientId(std::to_string(MAX_CLIENTS)), -1);
	EXPECT_EQ(VoiceLogic::ParseClientId("999999999999999999999"), -1);
	EXPECT_EQ(VoiceLogic::ParseClientId(std::string("3\0x", 3)), -1);
}

TEST(VoiceChat, RoomNameIgnoresInvalidAddress)
{
	EXPECT_EQ(VoiceLogic::SanitizeRoomName(nullptr, 0, ""), "");
	EXPECT_EQ(VoiceLogic::SanitizeRoomName("", 0, ""), "");
}

TEST(VoiceChat, RoomNameUsesServerAddress)
{
	EXPECT_EQ(VoiceLogic::SanitizeRoomName("46.174.54.240:8322", 0, ""), "46.174.54.240:8322");
	EXPECT_EQ(VoiceLogic::SanitizeRoomName("46.174.54.240:8322", 0, "friends"), "46.174.54.240:8322");
	EXPECT_EQ(VoiceLogic::SanitizeRoomName("46.174.54.240:8322", 1, ""), "46.174.54.240:8322");
	EXPECT_EQ(VoiceLogic::SanitizeRoomName("46.174.54.240:8322", 1, "friends"), "friends@46.174.54.240:8322");
}

TEST(VoiceChat, RoomNameSanitizesUnsupportedBytes)
{
	// IPv6 brackets and UTF-8 bytes are not allowed in a room name.
	EXPECT_EQ(VoiceLogic::SanitizeRoomName("[2001:db8::1]:8303", 0, ""), "_2001:db8::1_:8303");
	EXPECT_EQ(VoiceLogic::SanitizeRoomName("1.2.3.4:8303", 1, "a b/c@d\n"), "a_b_c@d_@1.2.3.4:8303");
	// One underscore per UTF-8 byte of "т".
	EXPECT_EQ(VoiceLogic::SanitizeRoomName("1.2.3.4:8303", 1, "\xD1\x82"), "__@1.2.3.4:8303");
}

TEST(VoiceChat, GainsMuteUnpositionedWhenFiltered)
{
	const vec2 Gains = VoiceLogic::VoiceGains(false, true, true, vec2(0.0f, 0.0f), 0, 50, 50, 100, 100, 25);
	EXPECT_FLOAT_EQ(Gains.x, 0.0f);
	EXPECT_FLOAT_EQ(Gains.y, 0.0f);
}

TEST(VoiceChat, GainsKeepUnpositionedAtMinimumVolume)
{
	// Filter disabled: audible at minimum volume, centered.
	const vec2 Gains = VoiceLogic::VoiceGains(false, true, false, vec2(0.0f, 0.0f), 40, 50, 50, 100, 100, 25);
	const float Expected = 0.4f * std::cos(pi / 4.0f);
	EXPECT_NEAR(Gains.x, Expected, 1e-5f);
	EXPECT_NEAR(Gains.y, Expected, 1e-5f);
}

TEST(VoiceChat, GainsFadeWithDistance)
{
	// Same tile: full volume.
	const vec2 Near = VoiceLogic::VoiceGains(true, true, false, vec2(0.0f, 0.0f), 0, 50, 50, 100, 0, 25);
	EXPECT_NEAR(Near.x, std::cos(pi / 4.0f), 1e-5f);
	EXPECT_NEAR(Near.y, std::sin(pi / 4.0f), 1e-5f);

	// Vertical distance attenuates as well.
	const vec2 Vertical = VoiceLogic::VoiceGains(true, true, false, vec2(0.0f, 1600.0f), 0, 50, 50, 100, 0, 25);
	EXPECT_LT(Vertical.x, Near.x);

	// Beyond the cutoff radius nothing is audible.
	const vec2 Far = VoiceLogic::VoiceGains(true, true, false, vec2(0.0f, 5000.0f), 0, 50, 50, 100, 100, 25);
	EXPECT_FLOAT_EQ(Far.x, 0.0f);
	EXPECT_FLOAT_EQ(Far.y, 0.0f);
}

TEST(VoiceChat, GainsPanTowardsTheSource)
{
	const vec2 Right = VoiceLogic::VoiceGains(true, true, false, vec2(100.0f, 0.0f), 100, 50, 50, 100, 100, 25);
	const vec2 Left = VoiceLogic::VoiceGains(true, true, false, vec2(-100.0f, 0.0f), 100, 50, 50, 100, 100, 25);
	EXPECT_GT(Right.y, Right.x);
	EXPECT_GT(Left.x, Left.y);

	// One stereo radius right is full pan.
	const vec2 FullRight = VoiceLogic::VoiceGains(true, true, false, vec2(800.0f, 0.0f), 100, 50, 50, 100, 100, 25);
	EXPECT_NEAR(FullRight.y, 1.0f, 1e-4f);
	EXPECT_NEAR(FullRight.x, 0.0f, 1e-4f);
}

TEST(VoiceChat, GainsScaleWithMasterVolume)
{
	const vec2 Full = VoiceLogic::VoiceGains(true, true, false, vec2(0.0f, 0.0f), 0, 50, 50, 100, 0, 25);
	const vec2 Half = VoiceLogic::VoiceGains(true, true, false, vec2(0.0f, 0.0f), 0, 50, 50, 50, 0, 25);
	EXPECT_NEAR(Half.x, Full.x * 0.5f, 1e-5f);
	EXPECT_NEAR(Half.y, Full.y * 0.5f, 1e-5f);

	const vec2 Silent = VoiceLogic::VoiceGains(true, true, false, vec2(0.0f, 0.0f), 0, 50, 50, 0, 0, 25);
	EXPECT_FLOAT_EQ(Silent.x, 0.0f);
	EXPECT_FLOAT_EQ(Silent.y, 0.0f);
}

TEST(VoiceChat, GainsRejectNonFiniteDelta)
{
	const vec2 NaN = VoiceLogic::VoiceGains(true, true, false, vec2(std::nanf(""), 0.0f), 100, 50, 50, 100, 100, 25);
	EXPECT_FLOAT_EQ(NaN.x, 0.0f);
	EXPECT_FLOAT_EQ(NaN.y, 0.0f);
	const vec2 Inf = VoiceLogic::VoiceGains(true, true, false, vec2(0.0f, INFINITY), 100, 50, 50, 100, 100, 25);
	EXPECT_FLOAT_EQ(Inf.x, 0.0f);
	EXPECT_FLOAT_EQ(Inf.y, 0.0f);
}

TEST(VoiceChat, LimiterKeepsHeadroom)
{
	EXPECT_FLOAT_EQ(VoiceLogic::LimiterTarget(0.0f), 1.0f);
	EXPECT_FLOAT_EQ(VoiceLogic::LimiterTarget(0.5f), 1.0f);
	EXPECT_FLOAT_EQ(VoiceLogic::LimiterTarget(0.9f), 1.0f);
	EXPECT_FLOAT_EQ(VoiceLogic::LimiterTarget(1.8f), 0.5f);
	EXPECT_FLOAT_EQ(VoiceLogic::LimiterTarget(9.0f), 0.1f);
	// NaN gain must not propagate.
	EXPECT_FLOAT_EQ(VoiceLogic::LimiterTarget(std::nanf("")), 1.0f);
	EXPECT_FLOAT_EQ(VoiceLogic::LimiterTarget(-1.0f), 1.0f);
}

TEST(VoiceChat, ShouldMuteSilencesDeadSession)
{
	VoiceLogic::SParticipantMix Mix;
	Mix.SessionActive = false;
	EXPECT_TRUE(VoiceLogic::ShouldMute(Mix));
}

TEST(VoiceChat, ShouldMuteRespectsTrackAndLocalMutes)
{
	VoiceLogic::SParticipantMix Mix;
	Mix.HasPosition = true;
	EXPECT_FALSE(VoiceLogic::ShouldMute(Mix));

	Mix.TrackMuted = true;
	EXPECT_TRUE(VoiceLogic::ShouldMute(Mix));
	Mix.TrackMuted = false;

	Mix.NameMuted = true;
	EXPECT_TRUE(VoiceLogic::ShouldMute(Mix));
	Mix.NameMuted = false;

	Mix.IsLocal = true;
	EXPECT_TRUE(VoiceLogic::ShouldMute(Mix));
}

TEST(VoiceChat, ShouldMutePositionFilterHasThreeModes)
{
	VoiceLogic::SParticipantMix Player;
	Player.HasPosition = false;

	// Mode 0: everyone audible, even without a position.
	Player.FilterMode = VoiceLogic::POSITION_FILTER_OFF;
	EXPECT_FALSE(VoiceLogic::ShouldMute(Player));

	// Mode 1: muted without a position, spectators kept.
	Player.FilterMode = VoiceLogic::POSITION_FILTER_PLAYERS;
	EXPECT_TRUE(VoiceLogic::ShouldMute(Player));
	VoiceLogic::SParticipantMix Spectator;
	Spectator.IsSpectator = true;
	Spectator.FilterMode = VoiceLogic::POSITION_FILTER_PLAYERS;
	EXPECT_FALSE(VoiceLogic::ShouldMute(Spectator));

	// Mode 2: spectators muted too.
	Spectator.FilterMode = VoiceLogic::POSITION_FILTER_ALL;
	EXPECT_TRUE(VoiceLogic::ShouldMute(Spectator));

	// A spectator with a character is just a player.
	VoiceLogic::SParticipantMix PositionedSpectator;
	PositionedSpectator.IsSpectator = true;
	PositionedSpectator.HasPosition = true;
	PositionedSpectator.FilterMode = VoiceLogic::POSITION_FILTER_ALL;
	EXPECT_FALSE(VoiceLogic::ShouldMute(PositionedSpectator));
}

TEST(VoiceChat, ShouldMuteTeamOnlyKeepsSpectatorsAndTeamless)
{
	VoiceLogic::SParticipantMix Teammate;
	Teammate.HasPosition = true;
	Teammate.TeamOnly = true;
	Teammate.MyTeamValid = true;
	Teammate.TeamMismatch = false;
	EXPECT_FALSE(VoiceLogic::ShouldMute(Teammate));

	VoiceLogic::SParticipantMix Opponent = Teammate;
	Opponent.TeamMismatch = true;
	EXPECT_TRUE(VoiceLogic::ShouldMute(Opponent));

	// Option off: opponent audible again.
	Opponent.TeamOnly = false;
	EXPECT_FALSE(VoiceLogic::ShouldMute(Opponent));

	// No real team of our own: nothing filtered.
	VoiceLogic::SParticipantMix Teamless = Teammate;
	Teamless.TeamMismatch = true;
	Teamless.MyTeamValid = false;
	EXPECT_FALSE(VoiceLogic::ShouldMute(Teamless));

	// Spectators never filtered by team.
	VoiceLogic::SParticipantMix Spectator = Teammate;
	Spectator.TeamMismatch = true;
	Spectator.IsSpectator = true;
	EXPECT_FALSE(VoiceLogic::ShouldMute(Spectator));
}

TEST(VoiceChat, MuteReasonReportsWhyATrackIsSilent)
{
	VoiceLogic::SParticipantMix Mix;
	Mix.HasPosition = true;
	EXPECT_EQ(VoiceLogic::MuteReasonOf(Mix), VoiceLogic::MUTE_NONE);
	EXPECT_FALSE(VoiceLogic::ShouldMute(Mix));
	EXPECT_EQ(VoiceLogic::MuteReasonLabel(VoiceLogic::MUTE_NONE), nullptr);

	Mix.SessionActive = false;
	EXPECT_EQ(VoiceLogic::MuteReasonOf(Mix), VoiceLogic::MUTE_SESSION);

	Mix.SessionActive = true;
	Mix.TrackMuted = true;
	EXPECT_EQ(VoiceLogic::MuteReasonOf(Mix), VoiceLogic::MUTE_TRACK);

	Mix.TrackMuted = false;
	Mix.NameMuted = true;
	EXPECT_EQ(VoiceLogic::MuteReasonOf(Mix), VoiceLogic::MUTE_NAME);

	Mix.NameMuted = false;
	Mix.IsLocal = true;
	EXPECT_EQ(VoiceLogic::MuteReasonOf(Mix), VoiceLogic::MUTE_LOCAL);

	// Priority: track mute wins over the position filter.
	Mix.IsLocal = false;
	Mix.TrackMuted = true;
	Mix.HasPosition = false;
	Mix.FilterMode = VoiceLogic::POSITION_FILTER_ALL;
	EXPECT_EQ(VoiceLogic::MuteReasonOf(Mix), VoiceLogic::MUTE_TRACK);

	Mix.TrackMuted = false;
	EXPECT_EQ(VoiceLogic::MuteReasonOf(Mix), VoiceLogic::MUTE_POSITION);

	Mix.HasPosition = true;
	Mix.TeamOnly = true;
	Mix.MyTeamValid = true;
	Mix.TeamMismatch = true;
	EXPECT_EQ(VoiceLogic::MuteReasonOf(Mix), VoiceLogic::MUTE_TEAM);
}

TEST(VoiceChat, MuteReasonLabelsCoverEveryReason)
{
	for(VoiceLogic::EMuteReason Reason : {VoiceLogic::MUTE_SESSION, VoiceLogic::MUTE_TRACK, VoiceLogic::MUTE_NAME,
		    VoiceLogic::MUTE_LOCAL, VoiceLogic::MUTE_POSITION, VoiceLogic::MUTE_TEAM, VoiceLogic::MUTE_NO_TRACK})
	{
		ASSERT_NE(VoiceLogic::MuteReasonLabel(Reason), nullptr);
		ASSERT_GT(std::string(VoiceLogic::MuteReasonLabel(Reason)).size(), 0u);
	}
}

TEST(VoiceChat, NextAutoConnectDelayBacksOff)
{
	EXPECT_FLOAT_EQ(VoiceLogic::NextAutoConnectDelay(0), 10.0f);
	EXPECT_FLOAT_EQ(VoiceLogic::NextAutoConnectDelay(1), 20.0f);
	EXPECT_FLOAT_EQ(VoiceLogic::NextAutoConnectDelay(2), 40.0f);
	EXPECT_FLOAT_EQ(VoiceLogic::NextAutoConnectDelay(3), 80.0f);
	EXPECT_FLOAT_EQ(VoiceLogic::NextAutoConnectDelay(5), 300.0f);
	// Capped at five minutes, safe for out-of-range input.
	EXPECT_FLOAT_EQ(VoiceLogic::NextAutoConnectDelay(6), 300.0f);
	EXPECT_FLOAT_EQ(VoiceLogic::NextAutoConnectDelay(1000), 300.0f);
	EXPECT_FLOAT_EQ(VoiceLogic::NextAutoConnectDelay(-5), 10.0f);
}

// Token from local_token.py, exp=1792922080.
static const char *const TEST_TOKEN =
	"eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
	"eyJuYW1lIjoiUGxheWVyIiwibWV0YWRhdGEiOiIwIiwidmlkZW8iOnsicm9vbUpvaW4iOnRydWUsInJvb20iOiJ0ZXN0IiwiY2FuUHVibGlzaCI6dHJ1ZSwiY2FuU3Vic2NyaWJlIjp0cnVlLCJjYW5QdWJsaXNoRGF0YSI6ZmFsc2UsImNhblB1Ymxpc2hTb3VyY2VzIjpbIm1pY3JvcGhvbmUiXSwiY2FuVXBkYXRlT3duTWV0YWRhdGEiOnRydWV9LCJzdWIiOiIxM2NkMGRiNTdhOWY0ZjU3OGE4NWZhNDBkMjkzMDM3NCIsImlzcyI6IkFQSU5KS2h1QUZFWFI0dCIsIm5iZiI6MTc5MjkyMTQ4MCwiZXhwIjoxNzkyOTIyMDgwfQ."
	"irjsTco2FsRos6sr8nZli7zkLRJriuzx89_BPBspKlw";

TEST(VoiceChat, TokenExpiryReadsExpClaim)
{
	EXPECT_EQ(VoiceLogic::TokenExpiry(TEST_TOKEN), 1792922080);
	// Not a JWT.
	EXPECT_EQ(VoiceLogic::TokenExpiry(""), 0);
	EXPECT_EQ(VoiceLogic::TokenExpiry("garbage"), 0);
	EXPECT_EQ(VoiceLogic::TokenExpiry("a.b"), 0);
	EXPECT_EQ(VoiceLogic::TokenExpiry("a.!!!.c"), 0);
	// No exp claim.
	EXPECT_EQ(VoiceLogic::TokenExpiry("a.bm90LWpzb24.c"), 0);
}

TEST(VoiceChat, TokenUnusableRejectsExpiredAndMalformed)
{
	const long long Expiry = 1792922080;
	// Before expiry, including the margin.
	EXPECT_FALSE(VoiceLogic::TokenUnusable(TEST_TOKEN, Expiry - 600));
	EXPECT_FALSE(VoiceLogic::TokenUnusable(TEST_TOKEN, Expiry - 60));
	// Inside the margin and after expiry.
	EXPECT_TRUE(VoiceLogic::TokenUnusable(TEST_TOKEN, Expiry - 10));
	EXPECT_TRUE(VoiceLogic::TokenUnusable(TEST_TOKEN, Expiry));
	EXPECT_TRUE(VoiceLogic::TokenUnusable(TEST_TOKEN, Expiry + 20 * 24 * 3600));
	// Empty and malformed.
	EXPECT_TRUE(VoiceLogic::TokenUnusable("", Expiry - 600));
	EXPECT_TRUE(VoiceLogic::TokenUnusable("not-a-jwt", Expiry - 600));
}

TEST(VoiceChat, DecodeBase64UrlHandlesPaddingAndUrlAlphabet)
{
	EXPECT_EQ(VoiceLogic::DecodeBase64Url("dGVzdA"), "test");
	EXPECT_EQ(VoiceLogic::DecodeBase64Url("dGVzdA=="), "test");
	EXPECT_EQ(VoiceLogic::DecodeBase64Url(""), "");
	EXPECT_EQ(VoiceLogic::DecodeBase64Url("!!!"), "");
	// '-' and '_' replace '+' and '/'.
	EXPECT_EQ(VoiceLogic::DecodeBase64Url("-_8"), VoiceLogic::DecodeBase64Url("+/8"));
}

TEST(VoiceChat, DeviceDelayMatchesPeriods)
{
	EXPECT_EQ(VoiceLogic::DeviceDelayMs(480, 2, 48000), 20);
	EXPECT_EQ(VoiceLogic::DeviceDelayMs(480, 1, 48000), 10);
	EXPECT_EQ(VoiceLogic::DeviceDelayMs(441, 2, 44100), 20);
	// No division by zero.
	EXPECT_EQ(VoiceLogic::DeviceDelayMs(0, 0, 0), 0);
}
