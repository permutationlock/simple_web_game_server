function computeQ(r: number) {
  return Math.pow(10, r/400.0);
}

function computeRatingChange(
    ratings: [ number, number ], scores: [ number, number ], k: number
  ): [ number, number]
{
  let qs = [ computeQ(ratings[0]), computeQ(ratings[1]) ];
  let denom = qs[0] + qs[1];
  let expectedScores = [ qs[0] / denom, qs[1] / denom];
  return [ ratings[0] + k * (scores[0] - expectedScores[0]),
           ratings[1] + k * (scores[1] - expectedScores[1]) ];
}


export { computeRatingChange };
