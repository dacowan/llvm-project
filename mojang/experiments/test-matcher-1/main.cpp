
#include <string>
#include <memory>
#include <vector>
#include <functional>

class PlayerProperties {
public:
	int getHealth() const { return mHealth; }
	void setHealth(int value) { mHealth = value; }
private:
	int mHealth = 1234;
};

class Player {
public:
	Player(const std::string& name) 
		: mName(name) {
	}

	void initializePlayer() {
		mProperties = std::make_unique<PlayerProperties>();
	}

	void shutdownPlayer() {
		mProperties.reset();
	}

	void killPlayer() {
	}

	void restorePlayer() {
		mProperties->setHealth(100);
	}

	void playerTick() {
		if (!mProperties) { return; }

		if (mProperties->getHealth() <= 0) {
			killPlayer();
		}
	}

	PlayerProperties* getProperties() const { return mProperties.get(); }

private:
	std::string mName;
	std::unique_ptr<PlayerProperties> mProperties;
};


class Engine {
public:
	void initializeEngine() {
		mPlayers.push_back(std::make_unique<Player>("Player-1"));
		//mPlayerTickFunctions.push_back([this]() {mPlayers[0]->playerTick();});
	}

	void shutdownEngine() {
		for (auto&& player : mPlayers) {
			player->shutdownPlayer();
		}
		mPlayers.clear();
	}

	bool tickPlayers() {
#if false
		for (auto&& fnTick : mPlayerTickFunctions) {
			fnTick();
		}
#else
		auto tickCaller = [this]() {
			for (auto&& player : mPlayers) {
				player->playerTick();
			}
		};

		tickCaller();
#endif

		return false;
	}

	bool tick() {
		const bool playersOK = tickPlayers();
		return playersOK;
	}

	Player* getPrimaryPlayer() {
		if (mPlayers.empty()) { return nullptr; }
		return mPlayers[0].get();
	}

private:
	using FN_PLAYER_TICK = std::function<void()>;

	std::vector<std::unique_ptr<Player>> mPlayers;

	std::vector<FN_PLAYER_TICK> mPlayerTickFunctions;
};

void ATestFunction(void) {
	return;
}

int main(int argc, char** argv) {

	ATestFunction();

	Engine gameEngine;

	gameEngine.initializeEngine();

	bool quit = false;
	while (!quit) {
		quit = gameEngine.tick();
	}


	gameEngine.shutdownEngine();

	return 0;
}