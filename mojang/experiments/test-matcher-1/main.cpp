
#include <string>
#include <memory>
#include <vector>
#include <functional>

#if false

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
	}

	void shutdownEngine() {
		for (auto&& player : mPlayers) {
			player->shutdownPlayer();
		}
		mPlayers.clear();
	}

	bool tickPlayers() {

		// Test a lambda for an indirect call to the ticks
		auto tickCaller = [this]() {
			for (auto&& player : mPlayers) {
				player->playerTick();
			}
		};

		tickCaller();  // invoke lambda
		return false;
	}

	bool tick() {
		bool playersOK = tickPlayers();
		bool tickPrimaryPlayerOK = tickPrimaryPlayer();
		return playersOK || tickPrimaryPlayerOK;
	}

	Player* getPrimaryPlayer() {
		if (mPlayers.empty()) { 
			return nullptr; 
		}
        Player* player = mPlayers[0].get();
                if (player) {
                        if (auto props = player->getProperties()) {
                          if (props->getHealth()) {
                            return player;
                          }
                        }
                }
                return nullptr;
	}
protected:
	bool tickPrimaryPlayer() {
		// just a nonsense function to get secondary invocations to our
		// focus function
		if( auto player = getPrimaryPlayer()) {
			player->playerTick();
			return false;
		}
		return true;
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

#else


#if false
void killPlayer() {
}

int getHealth() { 
	return 0; 
}

void playerTick() {
  int health = getHealth();
  if (health <= 0) {
    killPlayer();
  }
}

void checkHealth() {
  int health = getHealth();
  if (health <= 0) {
    killPlayer();
  }
}

void tick() { 
	playerTick();
  checkHealth();
}


int main(int argc, char **argv) { 

	tick();

	return 0; 
}
#else

class Test {
public:
  void testA() {}
};

void I() {
	// function of interest
}

void H() {
	I();
}

void G() {
	I();
}

void F() {
	H();
}

void E() {
	H();
}

void D() {
  F();
  G();
}

void C() {
  E();
  F();
}

void B() {
  C();
  D();
}

void A() { 
	Test t;

	B(); 

	t.testA();
}


#endif

#endif