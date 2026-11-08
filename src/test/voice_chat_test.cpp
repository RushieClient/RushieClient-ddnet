#include "test.h"

#include <game/client/components/rclient/voice/voice_chat_logic.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <string>
#include <utility>

TEST(VoiceChat, OverlaySortsSpeakersByStartAndSilentPlayersByRecentSound)
{
	// Input deliberately mixes client ids, activity and chronology.
	std::array<VoiceLogic::SOverlaySortKey, 7> aPlayers = {{
		{false, 10, 100, 0},
		{true, 40, 110, 1},
		{false, 0, 0, 4},
		{true, 20, 120, 9},
		{false, 15, 105, 8},
		{false, 0, 0, 2},
		{false, 0, 0, -1},
	}};
	std::sort(aPlayers.begin(), aPlayers.end());
	const std::array<int, 7> aExpectedIds = {9, 1, 8, 0, 2, 4, -1};
	for(size_t i = 0; i < aPlayers.size(); ++i)
		EXPECT_EQ(aPlayers[i].m_ClientId, aExpectedIds[i]);

	// More audio must not reorder an ongoing speech segment.
	aPlayers[0].m_LastSoundAt = 200;
	EXPECT_TRUE(aPlayers[0] < aPlayers[1]);
	// Once silent, the most recent speaker goes above older silent players.
	aPlayers[0].m_Speaking = false;
	std::sort(aPlayers.begin(), aPlayers.end());
	EXPECT_EQ(aPlayers[0].m_ClientId, 1);
	EXPECT_EQ(aPlayers[1].m_ClientId, 9);
	// Starting again places them below the person already speaking.
	aPlayers[1].m_Speaking = true;
	aPlayers[1].m_StartedAt = 210;
	EXPECT_TRUE(aPlayers[0] < aPlayers[1]);
}

TEST(VoiceChat, OverlaySortUsesClientIdForEqualTimes)
{
	for(bool Speaking : {false, true})
	{
		const VoiceLogic::SOverlaySortKey Low{Speaking, 10, 20, 1};
		const VoiceLogic::SOverlaySortKey High{Speaking, 10, 20, 2};
		const VoiceLogic::SOverlaySortKey Unknown{Speaking, 10, 20, -1};
		EXPECT_TRUE(Low < High);
		EXPECT_TRUE(High < Unknown);
		EXPECT_FALSE(High < Low);
		EXPECT_FALSE(Low < Low);
		EXPECT_FALSE(Unknown < Unknown);
	}
}

TEST(VoiceChat, OverlayDirectionPointsTowardsSnapshotDelta)
{
	for(const auto &[Delta, Expected] : {std::pair{vec2(100.0f, 0.0f), vec2(1.0f, 0.0f)}, std::pair{vec2(-100.0f, 0.0f), vec2(-1.0f, 0.0f)}, std::pair{vec2(0.0f, -100.0f), vec2(0.0f, -1.0f)}, std::pair{vec2(0.0f, 100.0f), vec2(0.0f, 1.0f)}})
	{
		const vec2 Direction = VoiceLogic::OverlayDirection(true, Delta);
		EXPECT_FLOAT_EQ(Direction.x, Expected.x);
		EXPECT_FLOAT_EQ(Direction.y, Expected.y);
	}
	const vec2 Diagonal = VoiceLogic::OverlayDirection(true, vec2(30.0f, 40.0f));
	EXPECT_NEAR(Diagonal.x, 0.6f, 1e-5f);
	EXPECT_NEAR(Diagonal.y, 0.8f, 1e-5f);
}

TEST(VoiceChat, OverlayDirectionUsesCircleWithoutValidBearing)
{
	const vec2 Unknown = VoiceLogic::OverlayDirection(false, vec2(100.0f, 200.0f));
	EXPECT_FLOAT_EQ(Unknown.x, 0.0f);
	EXPECT_FLOAT_EQ(Unknown.y, 0.0f);
	for(const vec2 Delta : {vec2(0.0f, 0.0f), vec2(std::nanf(""), 0.0f), vec2(0.0f, INFINITY)})
	{
		const vec2 Direction = VoiceLogic::OverlayDirection(true, Delta);
		EXPECT_FLOAT_EQ(Direction.x, 0.0f);
		EXPECT_FLOAT_EQ(Direction.y, 0.0f);
	}
}

TEST(VoiceChat, PttTailExpiresAtDeadline)
{
	VoiceLogic::CPttTail Tail;
	EXPECT_FALSE(Tail.Active(1000));
	Tail.Start(1000, 200);
	EXPECT_TRUE(Tail.Active(1000));
	EXPECT_TRUE(Tail.Active(1199));
	EXPECT_FALSE(Tail.Active(1200));
	EXPECT_FALSE(Tail.Active(1500));
}

TEST(VoiceChat, PttTailCanBeCancelledOrReplaced)
{
	VoiceLogic::CPttTail Tail;
	Tail.Start(1000, 200);
	Tail.Cancel();
	EXPECT_FALSE(Tail.Active(1050));
	Tail.Start(1100, 200);
	EXPECT_TRUE(Tail.Active(1299));
	EXPECT_FALSE(Tail.Active(1300));
	Tail.Start(1200, 0);
	EXPECT_FALSE(Tail.Active(1200));
}

TEST(VoiceChat, PttTailClampsConfiguredDelay)
{
	VoiceLogic::CPttTail Tail;
	Tail.Start(1000, -1);
	EXPECT_FALSE(Tail.Active(1000));
	Tail.Start(1000, 5000);
	EXPECT_TRUE(Tail.Active(1999));
	EXPECT_FALSE(Tail.Active(2000));
}

TEST(VoiceChat, OverlayVisibilityRespectsModeMuteAndActivity)
{
	for(bool Speaking : {false, true})
		for(bool Muted : {false, true})
		{
			EXPECT_EQ(VoiceLogic::ShowOverlayParticipant(Speaking, Muted, true, false), Speaking && !Muted);
			EXPECT_EQ(VoiceLogic::ShowOverlayParticipant(Speaking, Muted, true, true), Speaking);
			EXPECT_EQ(VoiceLogic::ShowOverlayParticipant(Speaking, Muted, false, false), !Muted);
			EXPECT_TRUE(VoiceLogic::ShowOverlayParticipant(Speaking, Muted, false, true));
		}
}

TEST(VoiceChat, MicrophonePeakHandlesSilenceAndEmptyInput)
{
	EXPECT_FLOAT_EQ(VoiceLogic::MicrophonePeak(nullptr, 0), 0.0f);
	const std::int16_t aSilence[] = {0, 0, 0, 0};
	EXPECT_FLOAT_EQ(VoiceLogic::MicrophonePeak(aSilence, std::size(aSilence)), 0.0f);
}

TEST(VoiceChat, MicrophonePeakHandlesBothPolaritiesAndFullScale)
{
	const std::int16_t aHalfScale[] = {0, -16384, 8192};
	EXPECT_FLOAT_EQ(VoiceLogic::MicrophonePeak(aHalfScale, std::size(aHalfScale)), 0.5f);
	const std::int16_t aFullScale[] = {-32768, 32767};
	EXPECT_FLOAT_EQ(VoiceLogic::MicrophonePeak(aFullScale, std::size(aFullScale)), 1.0f);
	const std::int16_t aQuiet[] = {1, -1};
	EXPECT_FLOAT_EQ(VoiceLogic::MicrophonePeak(aQuiet, std::size(aQuiet)), 1.0f / 32768.0f);
}

TEST(VoiceChat, DeafenMutesAllParticipantKinds)
{
	for(bool HasPosition : {false, true})
		for(bool IsSpectator : {false, true})
			for(int Team : std::array<int, 3>{TEAM_FLOCK, 1, TEAM_SUPER})
			{
				VoiceLogic::SParticipantMix Mix;
				Mix.HasPosition = HasPosition;
				Mix.IsSpectator = IsSpectator;
				Mix.MyTeam = 1;
				Mix.TheirTeam = Team;
				ASSERT_FALSE(VoiceLogic::ShouldMute(Mix));
				Mix.Deafened = true;
				EXPECT_EQ(VoiceLogic::MuteReasonOf(Mix), VoiceLogic::MUTE_DEAFENED);
				Mix.Deafened = false;
				EXPECT_FALSE(VoiceLogic::ShouldMute(Mix));
			}
}

TEST(VoiceChat, UndeafenRestoresExistingFiltersAndVolume)
{
	VoiceLogic::SParticipantMix Mix;
	Mix.HasPosition = true;
	Mix.MasterVolume = 75;
	Mix.UserVolume = 130;
	Mix.NameMuted = true;
	Mix.Deafened = true;
	EXPECT_EQ(VoiceLogic::MuteReasonOf(Mix), VoiceLogic::MUTE_DEAFENED);
	Mix.Deafened = false;
	EXPECT_EQ(VoiceLogic::MuteReasonOf(Mix), VoiceLogic::MUTE_NAME);
	EXPECT_EQ(Mix.MasterVolume, 75);
	EXPECT_EQ(Mix.UserVolume, 130);
	Mix.NameMuted = false;
	Mix.OtherTeamVolume = 0;
	Mix.Deafened = true;
	EXPECT_EQ(VoiceLogic::MuteReasonOf(Mix), VoiceLogic::MUTE_DEAFENED);
	Mix.Deafened = false;
	EXPECT_EQ(VoiceLogic::MuteReasonOf(Mix), VoiceLogic::MUTE_TEAM);
}

TEST(VoiceChat, MetadataRetryDelaysOnlyTheFailedClientId)
{
	VoiceLogic::CMetadataRetry Retry;
	EXPECT_TRUE(Retry.CanAttempt(1, 10.0));
	Retry.OnFailure(1, 10.0);
	EXPECT_FALSE(Retry.CanAttempt(1, 10.0));
	EXPECT_FALSE(Retry.CanAttempt(1, 11.999));
	EXPECT_TRUE(Retry.CanAttempt(1, 12.0));
	EXPECT_TRUE(Retry.CanAttempt(2, 10.0));
}

TEST(VoiceChat, MetadataRetryBacksOffAndCapsTheDelay)
{
	VoiceLogic::CMetadataRetry Retry;
	double Now = 100.0;
	for(double Delay : {2.0, 4.0, 8.0, 16.0, 30.0, 30.0, 30.0})
	{
		Retry.OnFailure(1, Now);
		EXPECT_FALSE(Retry.CanAttempt(1, Now + Delay - 0.001));
		EXPECT_TRUE(Retry.CanAttempt(1, Now + Delay));
		Now += Delay;
	}
}

TEST(VoiceChat, MetadataRetryStartsFreshForAnotherDummy)
{
	VoiceLogic::CMetadataRetry Retry;
	Retry.OnFailure(1, 0.0);
	Retry.OnFailure(1, 2.0);
	Retry.OnFailure(2, 2.5);
	EXPECT_TRUE(Retry.CanAttempt(1, 2.5));
	EXPECT_FALSE(Retry.CanAttempt(2, 4.499));
	EXPECT_TRUE(Retry.CanAttempt(2, 4.5));
}

TEST(VoiceChat, MetadataRetryResetsAfterSuccessOrSessionCleanup)
{
	VoiceLogic::CMetadataRetry Retry;
	Retry.OnFailure(1, 100.0);
	Retry.OnFailure(1, 102.0);
	Retry.Reset();
	EXPECT_TRUE(Retry.CanAttempt(1, 0.0));
	EXPECT_TRUE(Retry.CanAttempt(2, 0.0));
	Retry.OnFailure(1, 0.0);
	EXPECT_FALSE(Retry.CanAttempt(1, 1.999));
	EXPECT_TRUE(Retry.CanAttempt(1, 2.0));
}

TEST(VoiceChat, UserSettingsDefaultWithoutCreatingSavedEntry)
{
	VoiceLogic::CUserSettings Users;
	const auto User = Users.Get("Player");
	EXPECT_EQ(User.m_Name, "Player");
	EXPECT_EQ(User.m_Volume, 100);
	EXPECT_FALSE(User.m_Muted);
	EXPECT_TRUE(Users.All().empty());
}

TEST(VoiceChat, UserSettingsKeepVolumeAndMuteForMatchingNames)
{
	VoiceLogic::CUserSettings Users;
	ASSERT_TRUE(Users.Save(" Player ", 45, true));
	EXPECT_EQ(Users.Get("player").m_Volume, 45);
	EXPECT_TRUE(Users.Get("PLAYER").m_Muted);
	ASSERT_TRUE(Users.Save("PLAYER", 150, false));
	ASSERT_EQ(Users.All().size(), 1u);
	EXPECT_EQ(Users.Get("player").m_Volume, 150);
	EXPECT_FALSE(Users.Get(" Player ").m_Muted);
	EXPECT_EQ(Users.All()[0].m_Name, "PLAYER");
}

TEST(VoiceChat, RemovingSavedUserRestoresDefaults)
{
	VoiceLogic::CUserSettings Users;
	ASSERT_TRUE(Users.Save("Player", 25, true));
	ASSERT_TRUE(Users.Save("Other", 75, false));
	Users.Remove(" PLAYER ");
	EXPECT_EQ(Users.Get("Player").m_Volume, 100);
	EXPECT_FALSE(Users.Get("Player").m_Muted);
	ASSERT_EQ(Users.All().size(), 1u);
	EXPECT_EQ(Users.Get("Other").m_Volume, 75);
}

TEST(VoiceChat, UserSettingsCanRestoreSavedVolumeAndMute)
{
	VoiceLogic::CUserSettings Users;
	ASSERT_TRUE(Users.Save("Player", 0, false));
	ASSERT_TRUE(Users.Save("Nick \"quoted\" \\ path", 200, true));
	ASSERT_TRUE(Users.Save("\xD0\x98\xD0\xB3\xD1\x80\xD0\xBE\xD0\xBA", 80, false));
	VoiceLogic::CUserSettings Restored;
	for(const auto &User : Users.All())
		ASSERT_TRUE(Restored.Save(User.m_Name.c_str(), User.m_Volume, User.m_Muted));
	ASSERT_EQ(Restored.All().size(), Users.All().size());
	for(const auto &User : Users.All())
	{
		const auto Loaded = Restored.Get(User.m_Name.c_str());
		EXPECT_EQ(Loaded.m_Name, User.m_Name);
		EXPECT_EQ(Loaded.m_Volume, User.m_Volume);
		EXPECT_EQ(Loaded.m_Muted, User.m_Muted);
	}
}

TEST(VoiceChat, UserSettingsRejectInvalidNamesAndClampVolume)
{
	VoiceLogic::CUserSettings Users;
	for(const char *pName : {"", " \t ", "Player\ncommand", "Player\rcommand", "Player\tName"})
		EXPECT_FALSE(Users.Save(pName, 100, false));
	EXPECT_FALSE(Users.Save(nullptr, 100, false));
	EXPECT_FALSE(Users.Save(std::string(65, 'a').c_str(), 100, false));
	ASSERT_TRUE(Users.Save("Quiet", -50, false));
	ASSERT_TRUE(Users.Save("Loud", 900, false));
	EXPECT_EQ(Users.Get("Quiet").m_Volume, 0);
	EXPECT_EQ(Users.Get("Loud").m_Volume, 200);
}

TEST(VoiceChat, PersonalVolumeMultipliesOtherMixSettings)
{
	for(bool HasPosition : {false, true})
	{
		const vec2 Base = VoiceLogic::VoiceGains(HasPosition, true, 50, vec2(100.0f, 0.0f), 0, 50, 50, 70, 100, 25, 40);
		const vec2 Adjusted = VoiceLogic::VoiceGains(HasPosition, true, 50, vec2(100.0f, 0.0f), 0, 50, 50, 70, 100, 25, 40, 150);
		EXPECT_NEAR(Adjusted.x, Base.x * 1.5f, 1e-5f);
		EXPECT_NEAR(Adjusted.y, Base.y * 1.5f, 1e-5f);
		const vec2 Silent = VoiceLogic::VoiceGains(HasPosition, true, 50, vec2(100.0f, 0.0f), 0, 50, 50, 70, 100, 25, 40, 0);
		EXPECT_FLOAT_EQ(Silent.x, 0.0f);
		EXPECT_FLOAT_EQ(Silent.y, 0.0f);
	}
	VoiceLogic::SParticipantMix Mix;
	Mix.HasPosition = true;
	Mix.UserVolume = 0;
	EXPECT_EQ(VoiceLogic::MuteReasonOf(Mix), VoiceLogic::MUTE_VOLUME);
	Mix.UserVolume = 150;
	Mix.NameMuted = true;
	EXPECT_EQ(VoiceLogic::MuteReasonOf(Mix), VoiceLogic::MUTE_NAME);
}

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

TEST(VoiceChat, GainsMuteUnpositionedAtZeroGroupVolume)
{
	const vec2 Gains = VoiceLogic::VoiceGains(false, true, 0, vec2(0.0f, 0.0f), 100, 50, 50, 100, 100, 25);
	EXPECT_FLOAT_EQ(Gains.x, 0.0f);
	EXPECT_FLOAT_EQ(Gains.y, 0.0f);
}

TEST(VoiceChat, GainsKeepUnpositionedIndependentOfSpatialMinimum)
{
	const vec2 Gains = VoiceLogic::VoiceGains(false, true, 40, vec2(0.0f, 0.0f), 0, 50, 50, 100, 100, 25);
	const float Expected = 0.4f * std::cos(pi / 4.0f);
	EXPECT_NEAR(Gains.x, Expected, 1e-5f);
	EXPECT_NEAR(Gains.y, Expected, 1e-5f);
}

TEST(VoiceChat, GainsFadeBetweenFullAndMinimumDistances)
{
	for(const auto &[Tiles, Volume] : {std::pair{0.0f, 1.0f}, std::pair{10.0f, 1.0f}, std::pair{30.0f, 0.6f}, std::pair{50.0f, 0.2f}, std::pair{200.0f, 0.2f}})
	{
		const vec2 Gains = VoiceLogic::VoiceGains(true, true, 100, vec2(0.0f, Tiles * 32.0f), 20, 10, 50, 100, 0, 25);
		EXPECT_NEAR(Gains.x, Volume * std::cos(pi / 4.0f), 1e-5f);
		EXPECT_NEAR(Gains.y, Volume * std::sin(pi / 4.0f), 1e-5f);
	}
}

TEST(VoiceChat, DistantVoicesStayAtConfiguredMinimum)
{
	for(int Minimum : {0, 20, 100})
	{
		const vec2 Gains = VoiceLogic::VoiceGains(true, true, 100, vec2(8000.0f, 0.0f), Minimum, 10, 50, 100, 0, 25);
		EXPECT_NEAR(Gains.x, Minimum / 100.0f * std::cos(pi / 4.0f), 1e-5f);
		EXPECT_FLOAT_EQ(Gains.x, Gains.y);
	}
}

TEST(VoiceChat, FadeDistancesHandleEqualReversedAndOutOfRangeValues)
{
	for(const auto &[Start, End] : {std::pair{10, 10}, std::pair{50, 10}, std::pair{0, 0}, std::pair{-10, -20}, std::pair{500, 500}})
	{
		const vec2 Near = VoiceLogic::VoiceGains(true, true, 100, vec2(0.0f, 0.0f), 20, Start, End, 100, 0, 25);
		const vec2 Far = VoiceLogic::VoiceGains(true, true, 100, vec2(10000.0f, 0.0f), 20, Start, End, 100, 0, 25);
		EXPECT_NEAR(Near.x, std::cos(pi / 4.0f), 1e-5f);
		EXPECT_NEAR(Far.x, 0.2f * std::cos(pi / 4.0f), 1e-5f);
		EXPECT_TRUE(std::isfinite(Near.y));
		EXPECT_TRUE(std::isfinite(Far.y));
	}
}

TEST(VoiceChat, GainsPanTowardsTheSource)
{
	const vec2 Right = VoiceLogic::VoiceGains(true, true, 100, vec2(100.0f, 0.0f), 100, 50, 50, 100, 100, 25);
	const vec2 Left = VoiceLogic::VoiceGains(true, true, 100, vec2(-100.0f, 0.0f), 100, 50, 50, 100, 100, 25);
	EXPECT_GT(Right.y, Right.x);
	EXPECT_GT(Left.x, Left.y);

	// One stereo radius right is full pan.
	const vec2 FullRight = VoiceLogic::VoiceGains(true, true, 100, vec2(800.0f, 0.0f), 100, 50, 50, 100, 100, 25);
	EXPECT_NEAR(FullRight.y, 1.0f, 1e-4f);
	EXPECT_NEAR(FullRight.x, 0.0f, 1e-4f);
}

TEST(VoiceChat, GainsScaleWithMasterVolume)
{
	const vec2 Full = VoiceLogic::VoiceGains(true, true, 100, vec2(0.0f, 0.0f), 0, 50, 50, 100, 0, 25);
	const vec2 Half = VoiceLogic::VoiceGains(true, true, 100, vec2(0.0f, 0.0f), 0, 50, 50, 50, 0, 25);
	EXPECT_NEAR(Half.x, Full.x * 0.5f, 1e-5f);
	EXPECT_NEAR(Half.y, Full.y * 0.5f, 1e-5f);

	const vec2 Silent = VoiceLogic::VoiceGains(true, true, 100, vec2(0.0f, 0.0f), 0, 50, 50, 0, 0, 25);
	EXPECT_FLOAT_EQ(Silent.x, 0.0f);
	EXPECT_FLOAT_EQ(Silent.y, 0.0f);
}

TEST(VoiceChat, GainsRejectNonFiniteDelta)
{
	const vec2 NaN = VoiceLogic::VoiceGains(true, true, 100, vec2(std::nanf(""), 0.0f), 100, 50, 50, 100, 100, 25);
	EXPECT_FLOAT_EQ(NaN.x, 0.0f);
	EXPECT_FLOAT_EQ(NaN.y, 0.0f);
	const vec2 Inf = VoiceLogic::VoiceGains(true, true, 100, vec2(0.0f, INFINITY), 100, 50, 50, 100, 100, 25);
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

TEST(VoiceChat, GroupVolumesKeepSpectatorsAndUnpositionedPlayersAudible)
{
	VoiceLogic::SParticipantMix Player;
	Player.HasPosition = false;
	EXPECT_FALSE(VoiceLogic::ShouldMute(Player));
	EXPECT_EQ(VoiceLogic::ParticipantVolume(Player), 50);
	const vec2 PlayerGains = VoiceLogic::VoiceGains(false, true, VoiceLogic::ParticipantVolume(Player), vec2(0.0f, 0.0f), 0, 50, 50, 100, 100, 25);
	EXPECT_GT(PlayerGains.x, 0.0f);
	EXPECT_FLOAT_EQ(PlayerGains.x, PlayerGains.y);

	VoiceLogic::SParticipantMix Spectator;
	Spectator.IsSpectator = true;
	EXPECT_FALSE(VoiceLogic::ShouldMute(Spectator));
	const vec2 SpectatorGains = VoiceLogic::VoiceGains(false, true, VoiceLogic::ParticipantVolume(Spectator), vec2(0.0f, 0.0f), 0, 50, 50, 100, 100, 25);
	EXPECT_NEAR(SpectatorGains.x, PlayerGains.x * 2.0f, 1e-5f);
	EXPECT_FLOAT_EQ(SpectatorGains.x, SpectatorGains.y);
}

TEST(VoiceChat, GroupVolumesCanBeMutedIndependently)
{
	VoiceLogic::SParticipantMix Mix;
	Mix.UnpositionedVolume = 0;
	EXPECT_EQ(VoiceLogic::MuteReasonOf(Mix), VoiceLogic::MUTE_VOLUME);
	Mix.IsSpectator = true;
	EXPECT_FALSE(VoiceLogic::ShouldMute(Mix));
	Mix.SpectatorVolume = 0;
	EXPECT_EQ(VoiceLogic::MuteReasonOf(Mix), VoiceLogic::MUTE_VOLUME);
	// A spectator controlling a character uses ordinary positional audio.
	Mix.HasPosition = true;
	EXPECT_FALSE(VoiceLogic::ShouldMute(Mix));
	EXPECT_EQ(VoiceLogic::ParticipantVolume(Mix), 100);
	Mix.MasterVolume = 0;
	EXPECT_EQ(VoiceLogic::MuteReasonOf(Mix), VoiceLogic::MUTE_VOLUME);
}

TEST(VoiceChat, DistanceAndStereoSwitchesAreIndependent)
{
	for(bool Distance : {false, true})
		for(bool Stereo : {false, true})
		{
			const vec2 Near = VoiceLogic::VoiceGains(true, Distance, 100, vec2(640.0f, 0.0f), 0, 10, 50, 100, 100, 25, 100, 100, Stereo);
			const float Volume = Distance ? 0.75f : 1.0f;
			EXPECT_NEAR(Near.x * Near.x + Near.y * Near.y, Volume * Volume, 1e-5f);
			if(Stereo)
			{
				EXPECT_GT(Near.y, Near.x);
			}
			else
			{
				EXPECT_FLOAT_EQ(Near.x, Near.y);
			}

			const vec2 Far = VoiceLogic::VoiceGains(true, Distance, 100, vec2(8000.0f, 0.0f), 0, 10, 50, 100, 100, 25, 100, 100, Stereo);
			EXPECT_NEAR(Far.x * Far.x + Far.y * Far.y, Distance ? 0.0f : 1.0f, 1e-5f);
			if(!Stereo)
			{
				EXPECT_FLOAT_EQ(Far.x, Far.y);
			}
		}
}

TEST(VoiceChat, UnpositionedVoicesStayCenteredWithEitherSwitch)
{
	for(bool Distance : {false, true})
		for(bool Stereo : {false, true})
		{
			const vec2 Gains = VoiceLogic::VoiceGains(false, Distance, 50, vec2(8000.0f, 0.0f), 0, 50, 50, 100, 100, 25, 100, 100, Stereo);
			EXPECT_FLOAT_EQ(Gains.x, Gains.y);
			EXPECT_NEAR(Gains.x * Gains.x + Gains.y * Gains.y, 0.25f, 1e-5f);
		}
}

TEST(VoiceChat, GroupVolumeIgnoresDistanceAndScalesWithMaster)
{
	for(bool Spatial : {false, true})
	{
		const vec2 Gains = VoiceLogic::VoiceGains(false, Spatial, 200, vec2(5000.0f, 5000.0f), 0, 50, 50, 50, 200, 25);
		EXPECT_NEAR(Gains.x, std::cos(pi / 4.0f), 1e-5f);
		EXPECT_FLOAT_EQ(Gains.x, Gains.y);
	}
}

TEST(VoiceChat, TeamVolumeSeparatesTeammatesAndOtherTeams)
{
	VoiceLogic::SParticipantMix Teammate;
	Teammate.HasPosition = true;
	Teammate.MyTeam = 1;
	Teammate.TheirTeam = 1;
	Teammate.OtherTeamVolume = 0;
	EXPECT_FALSE(VoiceLogic::ShouldMute(Teammate));
	EXPECT_EQ(VoiceLogic::TeamVolume(Teammate), 100);

	VoiceLogic::SParticipantMix Opponent = Teammate;
	Opponent.TheirTeam = 2;
	EXPECT_TRUE(VoiceLogic::ShouldMute(Opponent));

	Opponent.OtherTeamVolume = 35;
	EXPECT_FALSE(VoiceLogic::ShouldMute(Opponent));
	EXPECT_EQ(VoiceLogic::TeamVolume(Opponent), 35);

	VoiceLogic::SParticipantMix Teamless = Teammate;
	Teamless.MyTeam = TEAM_FLOCK;
	Teamless.TheirTeam = TEAM_FLOCK;
	EXPECT_TRUE(VoiceLogic::ShouldMute(Teamless));

	VoiceLogic::SParticipantMix Spectator = Teammate;
	Spectator.TheirTeam = 2;
	Spectator.IsSpectator = true;
	EXPECT_FALSE(VoiceLogic::ShouldMute(Spectator));
}

TEST(VoiceChat, Team0AlwaysUsesOtherTeamVolume)
{
	for(const auto &Teams : {std::pair{0, 0}, std::pair{0, 1}, std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, -1}})
	{
		VoiceLogic::SParticipantMix Mix;
		Mix.MyTeam = Teams.first;
		Mix.TheirTeam = Teams.second;
		for(int Volume : {0, 25, 100, 200})
		{
			Mix.OtherTeamVolume = Volume;
			EXPECT_EQ(VoiceLogic::TeamVolume(Mix), Volume);
			EXPECT_EQ(VoiceLogic::ShouldMute(Mix), Volume == 0);
		}
	}
}

TEST(VoiceChat, SuperVolumeOverridesOtherTeamVolume)
{
	VoiceLogic::SParticipantMix Mix;
	Mix.HasPosition = true;
	Mix.OtherTeamVolume = 0;
	Mix.SuperVolume = 35;
	for(int SuperTeam : {TEAM_SUPER, LEGACY_TEAM_SUPER, VANILLA_TEAM_SUPER})
	{
		Mix.SuperTeam = SuperTeam;
		Mix.TheirTeam = SuperTeam;
		for(int MyTeam : {static_cast<int>(TEAM_FLOCK), 1, SuperTeam})
		{
			Mix.MyTeam = MyTeam;
			EXPECT_EQ(VoiceLogic::TeamVolume(Mix), 35);
			EXPECT_FALSE(VoiceLogic::ShouldMute(Mix));
		}
	}
	Mix.SuperVolume = 0;
	EXPECT_EQ(VoiceLogic::MuteReasonOf(Mix), VoiceLogic::MUTE_TEAM);
}

TEST(VoiceChat, TeamVolumeScalesPositionedAndUnpositionedVoices)
{
	VoiceLogic::SParticipantMix Mix;
	Mix.MyTeam = 1;
	Mix.TheirTeam = 2;
	Mix.OtherTeamVolume = 25;
	Mix.UnpositionedVolume = 40;
	for(bool HasPosition : {false, true})
	{
		Mix.HasPosition = HasPosition;
		for(bool Spatial : {false, true})
		{
			const vec2 Base = VoiceLogic::VoiceGains(HasPosition, Spatial, VoiceLogic::ParticipantVolume(Mix), vec2(100.0f, 0.0f), 0, 50, 50, 50, 100, 25);
			const vec2 Scaled = VoiceLogic::VoiceGains(HasPosition, Spatial, VoiceLogic::ParticipantVolume(Mix), vec2(100.0f, 0.0f), 0, 50, 50, 50, 100, 25, VoiceLogic::TeamVolume(Mix));
			EXPECT_GT(Base.x, 0.0f);
			EXPECT_NEAR(Scaled.x, Base.x * 0.25f, 1e-5f);
			EXPECT_NEAR(Scaled.y, Base.y * 0.25f, 1e-5f);
		}
	}
}

TEST(VoiceChat, Team0VolumePreservesLocalMutesAndGroupVolumes)
{
	VoiceLogic::SParticipantMix Mix;
	Mix.MyTeam = 1;
	Mix.TheirTeam = TEAM_FLOCK;
	Mix.OtherTeamVolume = 25;
	EXPECT_FALSE(VoiceLogic::ShouldMute(Mix));
	Mix.NameMuted = true;
	EXPECT_EQ(VoiceLogic::MuteReasonOf(Mix), VoiceLogic::MUTE_NAME);
	Mix.NameMuted = false;
	Mix.TrackMuted = true;
	EXPECT_EQ(VoiceLogic::MuteReasonOf(Mix), VoiceLogic::MUTE_TRACK);
	Mix.TrackMuted = false;
	Mix.UnpositionedVolume = 0;
	EXPECT_EQ(VoiceLogic::MuteReasonOf(Mix), VoiceLogic::MUTE_VOLUME);
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

	// Priority: track mute wins over a zero group volume.
	Mix.IsLocal = false;
	Mix.TrackMuted = true;
	Mix.HasPosition = false;
	Mix.UnpositionedVolume = 0;
	EXPECT_EQ(VoiceLogic::MuteReasonOf(Mix), VoiceLogic::MUTE_TRACK);

	Mix.TrackMuted = false;
	EXPECT_EQ(VoiceLogic::MuteReasonOf(Mix), VoiceLogic::MUTE_VOLUME);

	Mix.HasPosition = true;
	Mix.MyTeam = 1;
	Mix.TheirTeam = 2;
	Mix.OtherTeamVolume = 0;
	EXPECT_EQ(VoiceLogic::MuteReasonOf(Mix), VoiceLogic::MUTE_TEAM);
}

TEST(VoiceChat, MuteReasonLabelsCoverEveryReason)
{
	for(VoiceLogic::EMuteReason Reason : {VoiceLogic::MUTE_SESSION, VoiceLogic::MUTE_TRACK, VoiceLogic::MUTE_NAME,
		    VoiceLogic::MUTE_LOCAL, VoiceLogic::MUTE_VOLUME, VoiceLogic::MUTE_TEAM, VoiceLogic::MUTE_NO_TRACK})
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

// Fixed JWT fixture, exp=1792922080.
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

TEST(VoiceChat, DecodeBase64UrlRejectsBrokenPadding)
{
	for(const char *pInput : {"A", "=dGVzdA", "dG=VzdA=", "dGVzdA===", "dGVzdB==", "dGVzdA=A"})
		EXPECT_EQ(VoiceLogic::DecodeBase64Url(pInput), "");
}

TEST(VoiceChat, TokenExpiryRequiresIntegerAndThreeSegments)
{
	EXPECT_EQ(VoiceLogic::TokenExpiry("a.eyJleHAiOjEwMDB4fQ.c"), 0); // {"exp":1000x}
	EXPECT_EQ(VoiceLogic::TokenExpiry("a.eyJleHAiOjEwMDAuNX0.c"), 0); // {"exp":1000.5}
	EXPECT_EQ(VoiceLogic::TokenExpiry(std::string(TEST_TOKEN) + ".extra"), 0);
	EXPECT_TRUE(VoiceLogic::TokenUnusable(TEST_TOKEN, std::numeric_limits<long long>::max()));
}

TEST(VoiceChat, DeviceDelaySaturatesInsteadOfOverflowing)
{
	EXPECT_EQ(VoiceLogic::DeviceDelayMs(std::numeric_limits<unsigned>::max(), std::numeric_limits<unsigned>::max(), 1),
		std::numeric_limits<int>::max());
}

TEST(VoiceChat, DeviceDelayMatchesPeriods)
{
	EXPECT_EQ(VoiceLogic::DeviceDelayMs(480, 2, 48000), 20);
	EXPECT_EQ(VoiceLogic::DeviceDelayMs(480, 1, 48000), 10);
	EXPECT_EQ(VoiceLogic::DeviceDelayMs(441, 2, 44100), 20);
	// No division by zero.
	EXPECT_EQ(VoiceLogic::DeviceDelayMs(0, 0, 0), 0);
}
