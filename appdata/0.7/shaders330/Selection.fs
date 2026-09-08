#version 330 core

in float vTerrainGap;

uniform uint selectionId;

layout(location = 0) out uint selectionResult;

void main() {
    if(vTerrainGap > 0.0)
        discard;
    selectionResult = selectionId;
}
