#version 140

varying float vTerrainGap;

uniform uint selectionId;

out uint selectionResult;

void main() {
    if(vTerrainGap > 0.0)
        discard;
    selectionResult = selectionId;
}
