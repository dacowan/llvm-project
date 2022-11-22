
#include <string>
#include <memory>
#include <vector>

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

	void tick() {
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
	}

	void shutdownEngine() {
		for (auto&& player : mPlayers) {
			player->shutdownPlayer();
		}
		mPlayers.clear();
	}

	bool tickPlayers() {
		for (auto&& player : mPlayers) {
			player->tick();
		}
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
	std::vector<std::unique_ptr<Player>> mPlayers;
};


int main(int argc, char** argv) {

	Engine gameEngine;

	gameEngine.initializeEngine();

	bool quit = false;
	while (!quit) {
		quit = gameEngine.tick();
	}


	gameEngine.shutdownEngine();

	return 0;
}