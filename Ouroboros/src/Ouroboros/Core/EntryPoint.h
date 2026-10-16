#pragma once

// Include once, in the translation unit that defines Ouroboros::CreateApplication.

#include "Ouroboros/Core/Application.h"

int main(int argc, char** argv)
{
	Ouroboros::Application* application = Ouroboros::CreateApplication({ argc, argv });
	if (!application)
		return 1;

	const int exitCode = application->Run();
	delete application;
	return exitCode;
}
