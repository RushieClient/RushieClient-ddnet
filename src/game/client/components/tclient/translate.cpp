#include "translate.h"

#include <base/log.h>

#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>
#include <engine/shared/protocol.h>

#include <game/client/gameclient.h>
#include <game/client/lineinput.h>
#include <game/localization.h>

#include <algorithm>
#include <memory>

static void UrlEncode(const char *pText, char *pOut, size_t Length)
{
	if(Length == 0)
		return;
	size_t OutPos = 0;
	for(const char *p = pText; *p && OutPos < Length - 1; ++p)
	{
		unsigned char c = *(const unsigned char *)p;
		if(isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
		{
			if(OutPos >= Length - 1)
				break;
			pOut[OutPos++] = c;
		}
		else
		{
			if(OutPos + 3 >= Length)
				break;
			snprintf(pOut + OutPos, 4, "%%%02X", c);
			OutPos += 3;
		}
	}
	pOut[OutPos] = '\0';
}

static void UrlDecode(const char *pText, char *pOut, size_t Length)
{
	if(Length == 0)
		return;
	size_t OutPos = 0;
	for(const char *p = pText; *p && OutPos < Length - 1; ++p)
	{
		if(*p == '%' && p[1] && p[2])
		{
			char aHex[3] = {p[1], p[2], '\0'};
			pOut[OutPos++] = (char)strtol(aHex, nullptr, 16);
			p += 2;
		}
		else if(*p == '+')
		{
			pOut[OutPos++] = ' ';
		}
		else
		{
			pOut[OutPos++] = *p;
		}
	}
	pOut[OutPos] = '\0';
}

const char *ITranslateBackend::EncodeTarget(const char *pTarget) const
{
	if(!pTarget || pTarget[0] == '\0')
		return DefaultConfig::TcTranslateTarget;
	return pTarget;
}

bool ITranslateBackend::CompareTargets(const char *pA, const char *pB) const
{
	if(pA == pB) // if(!pA && !pB)
		return true;
	if(!pA || !pB)
		return false;
	if(str_comp_nocase(EncodeTarget(pA), EncodeTarget(pB)) == 0)
		return true;
	return false;
}

class ITranslateBackendHttp : public ITranslateBackend
{
protected:
	std::shared_ptr<CHttpRequest> m_pHttpRequest = nullptr;
	virtual bool ParseResponse(CTranslateResponse &Out) = 0;
	virtual bool ParseHttpError() const { return false; }

	void CreateHttpRequest(IHttp &Http, const char *pUrl)
	{
		auto pGet = std::make_shared<CHttpRequest>(pUrl);
		pGet->LogProgress(HTTPLOG::FAILURE);
		pGet->FailOnErrorStatus(false);
		pGet->Timeout(CTimeout{10000, 0, 500, 10});

		m_pHttpRequest = pGet;
		Http.Run(pGet);
	}

public:
	std::optional<bool> Update(CTranslateResponse &Out) override
	{
		dbg_assert(m_pHttpRequest != nullptr, "m_pHttpRequest is nullptr");
		if(m_pHttpRequest->State() == EHttpState::RUNNING || m_pHttpRequest->State() == EHttpState::QUEUED)
			return std::nullopt;
		if(m_pHttpRequest->State() == EHttpState::ABORTED)
		{
			str_copy(Out.m_Text, "Aborted");
			return false;
		}
		if(m_pHttpRequest->State() != EHttpState::DONE)
		{
			str_copy(Out.m_Text, "Curl error, see console");
			return false;
		}
		if(m_pHttpRequest->StatusCode() != 200 && !ParseHttpError())
		{
			str_format(Out.m_Text, sizeof(Out.m_Text), "Got http code %d", m_pHttpRequest->StatusCode());
			return false;
		}
		return ParseResponse(Out);
	}
	~ITranslateBackendHttp() override
	{
		if(m_pHttpRequest)
			m_pHttpRequest->Abort();
	}
	void Wait() override
	{
		if(m_pHttpRequest)
			m_pHttpRequest->Wait();
	}
};

class CTranslateBackendLibretranslate : public ITranslateBackendHttp
{
private:
	bool ParseResponseJson(const json_value *pObj, CTranslateResponse &Out)
	{
		if(!pObj)
		{
			str_copy(Out.m_Text, "Response is not JSON");
			return false;
		}

		if(pObj->type != json_object)
		{
			str_copy(Out.m_Text, "Response is not object");
			return false;
		}

		const json_value *pError = json_object_get(pObj, "error");
		if(pError != &json_value_none)
		{
			if(pError->type != json_string)
				str_copy(Out.m_Text, "Error is not string");
			else
				str_copy(Out.m_Text, pError->u.string.ptr);
			return false;
		}

		const json_value *pTranslatedText = json_object_get(pObj, "translatedText");
		if(pTranslatedText == &json_value_none)
		{
			str_copy(Out.m_Text, "No translatedText");
			return false;
		}
		if(pTranslatedText->type != json_string)
		{
			str_copy(Out.m_Text, "translatedText is not string");
			return false;
		}

		const json_value *pDetectedLanguage = json_object_get(pObj, "detectedLanguage");
		if(pDetectedLanguage == &json_value_none)
		{
			str_copy(Out.m_Text, "No pDetectedLanguage");
			return false;
		}
		if(pDetectedLanguage->type != json_object)
		{
			str_copy(Out.m_Text, "pDetectedLanguage is not object");
			return false;
		}

		const json_value *pConfidence = json_object_get(pDetectedLanguage, "confidence");
		if(pConfidence == &json_value_none || ((pConfidence->type == json_double && pConfidence->u.dbl == 0.0f) ||
							      (pConfidence->type == json_integer && pConfidence->u.integer == 0)))
		{
			str_copy(Out.m_Text, "Unknown language");
			return false;
		}

		const json_value *pLanguage = json_object_get(pDetectedLanguage, "language");
		if(pLanguage == &json_value_none)
		{
			str_copy(Out.m_Text, "No language");
			return false;
		}
		if(pLanguage->type != json_string)
		{
			str_copy(Out.m_Text, "language is not string");
			return false;
		}

		UrlDecode(pTranslatedText->u.string.ptr, Out.m_Text, sizeof(Out.m_Text));
		str_utf8_fix_truncation(Out.m_Text);
		str_copy(Out.m_Language, pLanguage->u.string.ptr);

		return true;
	}

protected:
	bool ParseResponse(CTranslateResponse &Out) override
	{
		json_value *pObj = m_pHttpRequest->ResultJson();
		bool Res = ParseResponseJson(pObj, Out);
		json_value_free(pObj);
		return Res;
	}
	bool ParseHttpError() const override { return true; }

public:
	const char *Name() const override
	{
		return "LibreTranslate";
	}
	CTranslateBackendLibretranslate(IHttp &Http, const char *pText, bool SendTranslate = false)
	{
		CJsonStringWriter Json = CJsonStringWriter();
		Json.BeginObject();
		Json.WriteAttribute("q");
		Json.WriteStrValue(pText);
		Json.WriteAttribute("source");
		Json.WriteStrValue("auto");
		Json.WriteAttribute("target");
		Json.WriteStrValue(ITranslateBackend::EncodeTarget(SendTranslate ? g_Config.m_RcTranslateSendTarget : g_Config.m_TcTranslateTarget));
		Json.WriteAttribute("format");
		Json.WriteStrValue("text");
		if(g_Config.m_TcTranslateKey[0] != '\0')
		{
			Json.WriteAttribute("api_key");
			Json.WriteStrValue(g_Config.m_TcTranslateKey);
		}
		Json.EndObject();
		CreateHttpRequest(Http, g_Config.m_TcTranslateEndpoint[0] == '\0' ? "localhost:5000/translate" : g_Config.m_TcTranslateEndpoint);
		const char *pJson = Json.GetOutputString().c_str();
		m_pHttpRequest->PostJson(pJson);
	}
};

class CTranslateBackendFtapi : public ITranslateBackendHttp
{
private:
	bool ParseResponseJson(const json_value *pObj, CTranslateResponse &Out)
	{
		if(!pObj)
		{
			str_copy(Out.m_Text, "Response is not JSON");
			return false;
		}

		if(pObj->type != json_object)
		{
			str_copy(Out.m_Text, "Response is not object");
			return false;
		}

		const json_value *pTranslatedText = json_object_get(pObj, "destination-text");
		if(pTranslatedText == &json_value_none)
		{
			str_copy(Out.m_Text, "No destination-text");
			return false;
		}
		if(pTranslatedText->type != json_string)
		{
			str_copy(Out.m_Text, "destination-text is not string");
			return false;
		}

		const json_value *pDetectedLanguage = json_object_get(pObj, "source-language");
		if(pDetectedLanguage == &json_value_none)
		{
			str_copy(Out.m_Text, "No source-language");
			return false;
		}
		if(pDetectedLanguage->type != json_string)
		{
			str_copy(Out.m_Text, "source-language is not string");
			return false;
		}

		UrlDecode(pTranslatedText->u.string.ptr, Out.m_Text, sizeof(Out.m_Text));
		str_utf8_fix_truncation(Out.m_Text);
		str_copy(Out.m_Language, pDetectedLanguage->u.string.ptr);

		return true;
	}

protected:
	bool ParseResponse(CTranslateResponse &Out) override
	{
		json_value *pObj = m_pHttpRequest->ResultJson();
		bool Res = ParseResponseJson(pObj, Out);
		json_value_free(pObj);
		return Res;
	}

public:
	const char *EncodeTarget(const char *pTarget) const override
	{
		if(!pTarget || pTarget[0] == '\0')
			return DefaultConfig::TcTranslateTarget;
		if(str_comp_nocase(pTarget, "zh") == 0)
			return "zh-cn";
		return pTarget;
	}
	const char *Name() const override
	{
		return "FreeTranslateAPI";
	}
	CTranslateBackendFtapi(IHttp &Http, const char *pText, bool SendTranslate = false)
	{
		char aBuf[4096];
		str_format(aBuf, sizeof(aBuf), "%s/translate?dl=%s&text=",
			g_Config.m_TcTranslateEndpoint[0] != '\0' ? g_Config.m_TcTranslateEndpoint : "https://ftapi.pythonanywhere.com",
			CTranslateBackendFtapi::EncodeTarget(SendTranslate ? g_Config.m_RcTranslateSendTarget : g_Config.m_TcTranslateTarget));

		UrlEncode(pText, aBuf + strlen(aBuf), sizeof(aBuf) - strlen(aBuf));

		CreateHttpRequest(Http, aBuf);
	}
};

//RClient
class CTranslateBackendGTX : public ITranslateBackendHttp
{
private:
	bool ParseResponseJson(const json_value *pObj, CTranslateResponse &Out)
	{
		if(!pObj)
		{
			str_copy(Out.m_Text, "Response is not JSON");
			return false;
		}

		if(pObj->type != json_array)
		{
			str_copy(Out.m_Text, "Response is not object");
			return false;
		}

		const json_value *pSentences = json_array_get(pObj, 0);
		if(pSentences == &json_value_none || pSentences->type != json_array)
		{
			str_copy(Out.m_Text, "No sentences array");
			return false;
		}

		Out.m_Text[0] = '\0';
		std::string AllStringsInJson;
		for(int i = 0; i < json_array_length(pSentences); i++)
		{
			const json_value *pTranslatedText = json_array_get(json_array_get(pSentences, i), 0);
			if(pTranslatedText == &json_value_none || pTranslatedText->type != json_string)
				continue;

			AllStringsInJson += pTranslatedText->u.string.ptr;
		}

		if(AllStringsInJson.c_str()[0] == '\0')
		{
			str_copy(Out.m_Text, "No destination-text");
			return false;
		}

		const json_value *pDetectedLanguage = json_array_get(pObj,2);
		if(pDetectedLanguage == &json_value_none)
		{
			str_copy(Out.m_Text, "No source-language");
			return false;
		}
		if(pDetectedLanguage->type != json_string)
		{
			str_copy(Out.m_Text, "source-language is not string");
			return false;
		}

		UrlDecode(AllStringsInJson.c_str(), Out.m_Text, sizeof(Out.m_Text));
		str_utf8_fix_truncation(Out.m_Text);
		str_copy(Out.m_Language, pDetectedLanguage->u.string.ptr);

		return true;
	}

protected:
	bool ParseResponse(CTranslateResponse &Out) override
	{
		json_value *pObj = m_pHttpRequest->ResultJson();
		bool Res = ParseResponseJson(pObj, Out);
		json_value_free(pObj);
		return Res;
	}

public:
	const char *EncodeTarget(const char *pTarget) const override
	{
		if(!pTarget || pTarget[0] == '\0')
			return DefaultConfig::TcTranslateTarget;
		return pTarget;
	}
	const char *Name() const override
	{
		return "googlegtx";
	}
	CTranslateBackendGTX(IHttp &Http, const char *pText, bool SendTranslate = false)
	{
		char aBuf[4096];
		str_format(aBuf, sizeof(aBuf), "%s/translate_a/single?client=gtx&sl=auto&tl=%s&dt=t&q=",
			g_Config.m_TcTranslateEndpoint[0] != '\0' ? g_Config.m_TcTranslateEndpoint : "https://translate.google.com",
			CTranslateBackendGTX::EncodeTarget(SendTranslate ? g_Config.m_RcTranslateSendTarget : g_Config.m_TcTranslateTarget));

		UrlEncode(pText, aBuf + strlen(aBuf), sizeof(aBuf) - strlen(aBuf));
		CreateHttpRequest(Http, aBuf);
	}
};

class CTranslateBackendFedilab : public ITranslateBackendHttp
{
private:
	bool ParseResponseJson(const json_value *pObj, CTranslateResponse &Out)
	{
		if(!pObj)
		{
			str_copy(Out.m_Text, "Response is not JSON");
			return false;
		}

		if(pObj->type != json_object)
		{
			str_copy(Out.m_Text, "Response is not object");
			return false;
		}

		const json_value *pError = json_object_get(pObj, "error");
		if(pError != &json_value_none)
		{
			if(pError->type != json_string)
				str_copy(Out.m_Text, "Error is not string");
			else
				str_copy(Out.m_Text, pError->u.string.ptr);
			return false;
		}

		const json_value *pTranslatedText = json_object_get(pObj, "translatedText");
		if(pTranslatedText == &json_value_none)
		{
			str_copy(Out.m_Text, "No translatedText");
			return false;
		}
		if(pTranslatedText->type != json_string)
		{
			str_copy(Out.m_Text, "translatedText is not string");
			return false;
		}

		const json_value *pDetectedLanguage = json_object_get(pObj, "detectedLanguage");
		if(pDetectedLanguage == &json_value_none)
		{
			str_copy(Out.m_Text, "No pDetectedLanguage");
			return false;
		}
		if(pDetectedLanguage->type != json_object)
		{
			str_copy(Out.m_Text, "pDetectedLanguage is not object");
			return false;
		}

		const json_value *pConfidence = json_object_get(pDetectedLanguage, "confidence");
		if(pConfidence == &json_value_none || ((pConfidence->type == json_double && pConfidence->u.dbl == 0.0f) ||
							      (pConfidence->type == json_integer && pConfidence->u.integer == 0)))
		{
			str_copy(Out.m_Text, "Unknown language");
			return false;
		}

		const json_value *pLanguage = json_object_get(pDetectedLanguage, "language");
		if(pLanguage == &json_value_none)
		{
			str_copy(Out.m_Text, "No language");
			return false;
		}
		if(pLanguage->type != json_string)
		{
			str_copy(Out.m_Text, "language is not string");
			return false;
		}

		UrlDecode(pTranslatedText->u.string.ptr, Out.m_Text, sizeof(Out.m_Text));
		str_utf8_fix_truncation(Out.m_Text);
		str_copy(Out.m_Language, pLanguage->u.string.ptr);

		return true;
	}

protected:
	bool ParseResponse(CTranslateResponse &Out) override
	{
		json_value *pObj = m_pHttpRequest->ResultJson();
		bool Res = ParseResponseJson(pObj, Out);
		json_value_free(pObj);
		return Res;
	}
	bool ParseHttpError() const override { return true; }

public:
	const char *Name() const override
	{
		return "Fedilab";
	}
	CTranslateBackendFedilab(IHttp &Http, const char *pText, bool SendTranslate = false)
	{
		CJsonStringWriter Json = CJsonStringWriter();
		Json.BeginObject();
		Json.WriteAttribute("q");
		Json.WriteStrValue(pText);
		Json.WriteAttribute("source");
		Json.WriteStrValue("auto");
		Json.WriteAttribute("target");
		Json.WriteStrValue(EncodeTarget(g_Config.m_TcTranslateTarget));
		Json.WriteAttribute("format");
		Json.WriteStrValue("text");
		if((!SendTranslate && g_Config.m_TcTranslateKey[0] != '\0') || (SendTranslate && g_Config.m_RcTranslateSendTarget[0] != '\0'))
		{
			Json.WriteAttribute("api_key");
			Json.WriteStrValue(SendTranslate ? g_Config.m_RcTranslateSendTarget : g_Config.m_TcTranslateTarget);
		}
		Json.EndObject();
		CreateHttpRequest(Http, g_Config.m_TcTranslateEndpoint[0] == '\0' ? "https://translate.fedilab.app/translate" : g_Config.m_TcTranslateEndpoint);
		const char *pJson = Json.GetOutputString().c_str();
		m_pHttpRequest->PostJson(pJson);
	}
};

void CTranslate::ConTranslate(IConsole::IResult *pResult, void *pUserData)
{
	const char *pName;
	if(pResult->NumArguments() == 0)
		pName = nullptr;
	else
		pName = pResult->GetString(0);

	CTranslate *pThis = static_cast<CTranslate *>(pUserData);
	pThis->Translate(pName);
}

void CTranslate::ConTranslateId(IConsole::IResult *pResult, void *pUserData)
{
	CTranslate *pThis = static_cast<CTranslate *>(pUserData);
	pThis->Translate(pResult->GetInteger(0));
}

void CTranslate::OnConsoleInit()
{
	Console()->Register("translate", "?r[name]", CFGFLAG_CLIENT, ConTranslate, this, "Translate last message (of a given name)");
	Console()->Register("translate_id", "v[id]", CFGFLAG_CLIENT, ConTranslateId, this, "Translate last message of the person with this id");
}

void CTranslate::Translate(int Id, bool ShowProgress)
{
	if(Id < 0 || Id > (int)std::size(GameClient()->m_aClients))
	{
		GameClient()->m_Chat.Echo("Not a valid ID");
		return;
	}
	const auto &Player = GameClient()->m_aClients[Id];
	if(!Player.m_Active)
	{
		GameClient()->m_Chat.Echo("ID not connected");
		return;
	}
	Translate(Player.m_aName, ShowProgress);
}

void CTranslate::Translate(const char *pName, bool ShowProgress)
{
	CChat::CLine *pLineBest = nullptr;
	if(GameClient()->m_Chat.m_CurrentLine > 0)
	{
		int ScoreBest = -1;
		for(int i = 0; i < CChat::MAX_LINES; i++)
		{
			CChat::CLine *pLine = &GameClient()->m_Chat.m_aLines[((GameClient()->m_Chat.m_CurrentLine - i) + CChat::MAX_LINES) % CChat::MAX_LINES];
			if(pLine->m_pTranslateResponse != nullptr)
				continue;
			if(pLine->m_ClientId == CChat::CLIENT_MSG)
				continue;
			for(int Id : GameClient()->m_aLocalIds)
				if(pLine->m_ClientId == Id)
					continue;
			int Score = 0;
			if(pName)
			{
				if(pLine->m_ClientId == CChat::SERVER_MSG)
					continue;
				if(str_comp(pLine->m_aName, pName) == 0)
					Score = 2;
				else if(str_comp_nocase(pLine->m_aName, pName) == 0)
					Score = 1;
				else
					continue;
			}
			if(Score > ScoreBest)
			{
				ScoreBest = Score;
				pLineBest = pLine;
			}
		}
	}
	if(!pLineBest || pLineBest->m_aText[0] == '\0')
	{
		GameClient()->m_Chat.Echo("No message to translate");
		return;
	}

	Translate(*pLineBest, ShowProgress);
}

void CTranslate::Translate(CChat::CLine &Line, bool ShowProgress)
{
	if(m_vJobs.size() > 15)
	{
		return;
	}

	CTranslateJob Job;
	Job.m_pLine = &Line;
	Job.m_pTranslateResponse = std::make_shared<CTranslateResponse>();
	Job.m_pLine->m_pTranslateResponse = Job.m_pTranslateResponse;

	const char *pTextToTranslate = Line.m_aText;
	const char *pColon = str_find(Line.m_aText, ": ");
	if(pColon && pColon != Line.m_aText)
	{
		size_t PrefixLen = pColon - Line.m_aText + 2;
		if(PrefixLen < sizeof(Job.m_TextPrefix))
		{
			str_copy(Job.m_TextPrefix, Line.m_aText, PrefixLen + 1);
			pTextToTranslate = Line.m_aText + PrefixLen;
		}
	}

	if(str_comp_nocase(g_Config.m_TcTranslateBackend, "libretranslate") == 0)
		Job.m_pBackend = std::make_unique<CTranslateBackendLibretranslate>(*Http(), pTextToTranslate);
	else if(str_comp_nocase(g_Config.m_TcTranslateBackend, "ftapi") == 0)
		Job.m_pBackend = std::make_unique<CTranslateBackendFtapi>(*Http(), pTextToTranslate);
	else if(str_comp_nocase(g_Config.m_TcTranslateBackend, "googlegtx") == 0)
		Job.m_pBackend = std::make_unique<CTranslateBackendGTX>(*Http(), pTextToTranslate);
	else if(str_comp_nocase(g_Config.m_TcTranslateBackend, "fedilab") == 0)
		Job.m_pBackend = std::make_unique<CTranslateBackendFedilab>(*Http(), pTextToTranslate);
	else
	{
		GameClient()->m_Chat.Echo("Invalid translate backend");
		return;
	}

	if(ShowProgress)
	{
		str_format(Job.m_pTranslateResponse->m_Text, sizeof(Job.m_pTranslateResponse->m_Text), TCLocalize("%s translating to %s", "translate"), Job.m_pBackend->Name(), g_Config.m_TcTranslateTarget);
		Job.m_pLine->m_Time = time();
	}
	else
	{
		Job.m_pTranslateResponse->m_Text[0] = '\0';
	}

	m_vJobs.emplace_back(std::move(Job));

	if(ShowProgress)
		GameClient()->m_Chat.RebuildChat();
}

void CTranslate::OnRender()
{
	const auto Time = time();
	auto ForEach = [&](CTranslateJob &Job) {
		if(!Job.m_pIsTextTranslate)
		{
			if(Job.m_pLine->m_pTranslateResponse != Job.m_pTranslateResponse)
				return true; // Not the same line anymore
			const std::optional<bool> Done = Job.m_pBackend->Update(*Job.m_pTranslateResponse);
			if(!Done.has_value())
				return false; // Keep ongoing tasks
			if(*Done)
			{
				if(str_comp_nocase(Job.m_pLine->m_aText, Job.m_pTranslateResponse->m_Text) == 0) // Check for no translation difference
					Job.m_pTranslateResponse->m_Text[0] = '\0';
				else if(Job.m_TextPrefix[0] != '\0')
				{
					char aBuf[sizeof(Job.m_pTranslateResponse->m_Text)];
					str_format(aBuf, sizeof(aBuf), "%s%s", Job.m_TextPrefix, Job.m_pTranslateResponse->m_Text);
					str_copy(Job.m_pTranslateResponse->m_Text, aBuf);
					if(str_comp_nocase(Job.m_pLine->m_aText, Job.m_pTranslateResponse->m_Text) == 0) // Check for no translation difference
						Job.m_pTranslateResponse->m_Text[0] = '\0';
				}
			}
			else
			{
				char aBuf[sizeof(Job.m_pTranslateResponse->m_Text)];
				str_format(aBuf, sizeof(aBuf), TCLocalize("%s to %s failed: %s", "translate"), Job.m_pBackend->Name(), g_Config.m_TcTranslateTarget, Job.m_pTranslateResponse->m_Text);
				Job.m_pTranslateResponse->m_Error = true;
				str_copy(Job.m_pTranslateResponse->m_Text, aBuf);
			}
			Job.m_pLine->m_Time = Time;
			GameClient()->m_Chat.RebuildChat();
			return true;
		}
		else
		{
			if(Job.m_pLineTranslate->m_pTranslateResponse != Job.m_pTranslateResponse)
				return true; // Not the same line anymore
			const std::optional<bool> Done = Job.m_pBackend->Update(*Job.m_pTranslateResponse);
			if(!Done.has_value())
				return false; // Keep ongoing tasks
			if(*Done)
			{
				if(str_comp_nocase(Job.m_pLineTranslate->m_aText, Job.m_pTranslateResponse->m_Text) == 0) // Check for no translation difference
					str_copy(Job.m_pTranslateResponse->m_Text, Job.m_pLineTranslate->m_aText, sizeof(Job.m_pTranslateResponse->m_Text));
				else if(Job.m_TextPrefix[0] != '\0')
				{
					char aBuf[sizeof(Job.m_pTranslateResponse->m_Text)];
					str_format(aBuf, sizeof(aBuf), "%s%s", Job.m_TextPrefix, Job.m_pTranslateResponse->m_Text);
					str_copy(Job.m_pTranslateResponse->m_Text, aBuf);
				}
			}
			else
			{
				char aBuf[sizeof(Job.m_pTranslateResponse->m_Text)];
				str_format(aBuf, sizeof(aBuf), TCLocalize("%s to %s failed: %s", "translate"), Job.m_pBackend->Name(), g_Config.m_TcTranslateTarget, Job.m_pTranslateResponse->m_Text);
				Job.m_pTranslateResponse->m_Error = true;
				str_copy(Job.m_pTranslateResponse->m_Text, aBuf);
			}
			GameClient()->m_RClient.DoTranslateWork(*Job.m_pTranslateResponse, *Job.m_pLineTranslate);
			return true;
		}
	};
	m_vJobs.erase(std::remove_if(m_vJobs.begin(), m_vJobs.end(), ForEach), m_vJobs.end());
}

void CTranslate::AutoTranslate(CChat::CLine &Line)
{
	if(!g_Config.m_TcTranslateAuto)
		return;
	if(Line.m_ClientId == CChat::CLIENT_MSG || (!g_Config.m_RcTranslateServerMessages && Line.m_ClientId == CChat::SERVER_MSG))
		return;
	for(const int Id : GameClient()->m_aLocalIds)
	{
		if(Id >= 0 && Id == Line.m_ClientId)
			return;
	}
	if(str_comp(g_Config.m_TcTranslateBackend, "ftapi") == 0)
	{
		// FTAPI quickly gets overloaded, please do not disable this
		// It may shut down if we spam it too hard
		return;
	}
	Translate(Line, false);
}

// Rushie
void CTranslate::TranslateSend(const char *Line, int WorkId, int m_JobIntVariable)
{
	if(m_vJobs.size() > 15)
	{
		return;
	}

	CTranslateJob Job;
	Job.m_pLineTranslate = std::make_unique<CRClient::CLineTranslate>();
	Job.m_pLineTranslate->m_JobIntVariable = m_JobIntVariable;
	Job.m_pIsTextTranslate = true;
	Job.m_pLineTranslate->m_WorkId = WorkId;

	const char *pTextToTranslate = Line;
	const char *pColon = str_find(Line, ": ");
	if(pColon && pColon != Line)
	{
		size_t PrefixLen = pColon - Line + 2;
		if(PrefixLen < sizeof(Job.m_TextPrefix))
		{
			str_copy(Job.m_TextPrefix, Line, PrefixLen + 1);
			pTextToTranslate = Line + PrefixLen;
		}
	}

	str_copy(Job.m_pLineTranslate->m_aText, pTextToTranslate);
	Job.m_pTranslateResponse = std::make_shared<CTranslateResponse>();
	Job.m_pLineTranslate->m_pTranslateResponse = Job.m_pTranslateResponse;

	if(str_comp_nocase(g_Config.m_TcTranslateBackend, "libretranslate") == 0)
		Job.m_pBackend = std::make_unique<CTranslateBackendLibretranslate>(*Http(), Job.m_pLineTranslate->m_aText, true);
	else if(str_comp_nocase(g_Config.m_TcTranslateBackend, "ftapi") == 0)
		Job.m_pBackend = std::make_unique<CTranslateBackendFtapi>(*Http(), Job.m_pLineTranslate->m_aText, true);
	else if(str_comp_nocase(g_Config.m_TcTranslateBackend, "googlegtx") == 0)
		Job.m_pBackend = std::make_unique<CTranslateBackendGTX>(*Http(), Job.m_pLineTranslate->m_aText, true);
	else if(str_comp_nocase(g_Config.m_TcTranslateBackend, "fedilab") == 0)
		Job.m_pBackend = std::make_unique<CTranslateBackendFedilab>(*Http(), Job.m_pLineTranslate->m_aText, true);
	else
	{
		GameClient()->m_Chat.Echo("Invalid translate backend");
		return;
	}

	Job.m_pTranslateResponse->m_Text[0] = '\0';
	m_vJobs.emplace_back(std::move(Job));
}