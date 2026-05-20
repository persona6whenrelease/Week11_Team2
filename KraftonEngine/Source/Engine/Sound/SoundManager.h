#pragma once
#include "Core/CoreTypes.h"
#include "Core/Singleton.h"

#include "ThirdParty/SFML/Audio.hpp"

using FSoundId = FString;

class FSoundManager : public TSingleton<FSoundManager>
{
	friend class TSingleton<FSoundManager>;

public:
	void initialize();

	bool LoadMusic(const FSoundId& ID, const std::wstring& FilePath, bool bLoop = true);
	void PlayMusic(const FSoundId& ID);
	void StopMusic(const FSoundId& ID);
	void StopAllMusic();

	// Legacy aliases for callers that only need one active music channel.
	void PlayBGM();
	void StopBGM();

	void LoadEffect(const FSoundId& ID, const std::wstring& FilePath);
	// 지정 디렉토리 내 모든 .wav를 stem(확장자 없는 파일명)을 ID로 사용해 LoadEffect 등록.
	// 개별 파일 실패는 로그만 남기고 계속 진행.
	void ScanAndLoadEffectsFromDirectory(const std::wstring& Directory);
	void PlayEffect(const FSoundId& ID);
	void StopEffect(const FSoundId& ID);

	bool IsEffectPlaying(const FSoundId& ID) const;
	float GetEffectDuration(const FSoundId& ID) const;

	// LoadEffect로 등록된 효과음 ID 목록을 알파벳 정렬해 반환한다. 에디터의 SoundId 드롭다운용.
	TArray<FSoundId> GetRegisteredEffectIds() const;

private:
	FSoundManager() = default;
	TMap<FSoundId, std::unique_ptr<sf::SoundBuffer>> SoundBufferMap;
	TMap<FSoundId, std::unique_ptr<sf::Sound>> Sounds;
	TMap<FSoundId, std::unique_ptr<sf::Music>> MusicMap;
	FSoundId LastMusicId;
};
